#include "net.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include <string.h>
#include "camera.h"
#include "ai.h"
#include "armlink.h"
#include "armcal.h"
#include "armctrl.h"

static const char *TAG = "http_srv";

static esp_err_t root_get(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ChargeReborn 采集</title></head>"
        "<body style='font-family:sans-serif;text-align:center'>"
        "<h3>ChargeReborn 数据采集</h3>"
        "<div>类名 <input id=n value='18650' size=8> "
        "间隔 <input id=iv value='1.5' size=3>秒 "
        "<button onclick='shot()'>抓拍</button> "
        "<button id=run onclick='toggle()'>连拍开始</button> "
        "已存 <span id=c>0</span> 张</div>"
        "<div><button id=det onclick='dtog()'>识别开始</button> <span id=ds>-</span></div>"
        "<div style='margin:6px'><button onclick='atest()'>机械臂测试帧</button> "
        "<span id=as>-</span></div>"
        "<div style='margin:6px'>G级 <select id=gr onchange='gset()'>"
        "<option value=0>G0</option><option value=1>G1</option><option value=2>G2</option>"
        "<option value=3>G3</option><option value=4>G4</option></select> "
        "<button onclick='arun(1)'>抓取启动</button> "
        "<button onclick='arun(0)'>停止</button> <span id=gs>-</span></div>"
        "<p style='position:relative;display:inline-block;line-height:0'>"
        "<img id=v style='max-width:96vw;display:block'>"
        "<canvas id=ov style='position:absolute;left:0;top:0;pointer-events:none'></canvas>"
        "</p>"
        "<script>"
        "var t=null,cnt=0;"
        "function nm(){return encodeURIComponent(document.getElementById('n').value||'cap');}"
        "function shot(){"
        "fetch('/capture?name='+nm()).then(function(r){return r.blob();}).then(function(b){"
        "var a=document.createElement('a');a.href=URL.createObjectURL(b);"
        "a.download=(document.getElementById('n').value||'cap')+'_'+Date.now()+'.jpg';"
        "a.click();URL.revokeObjectURL(a.href);"
        "document.getElementById('c').textContent=++cnt;});}"
        "function toggle(){var b=document.getElementById('run');"
        "if(t){clearInterval(t);t=null;b.textContent='连拍开始';return;}"
        "var ms=Math.max(300,parseFloat(document.getElementById('iv').value||'1.5')*1000);"
        "t=setInterval(shot,ms);b.textContent='连拍停止';}"
        "var dt=null;"
        "function draw(d){var im=document.getElementById('v'),cv=document.getElementById('ov');"
        "cv.width=im.clientWidth;cv.height=im.clientHeight;"
        "var g=cv.getContext('2d');g.clearRect(0,0,cv.width,cv.height);"
        "document.getElementById('ds').textContent='n='+d.n+' '+d.infer_ms+'ms';"
        "if(!d.w||!d.n){return;}var sx=cv.width/d.w,sy=cv.height/d.h;"
        "g.lineWidth=2;g.strokeStyle='#0f0';g.fillStyle='#0f0';g.font='14px sans-serif';"
        "for(var i=0;i<d.boxes.length;i++){var b=d.boxes[i];"
        "var x=b.x1*sx,y=b.y1*sy;"
        "g.strokeRect(x,y,(b.x2-b.x1)*sx,(b.y2-b.y1)*sy);"
        "g.fillText(b.name+' '+b.s.toFixed(2),x+2,y>14?y-3:y+12);"
        "if(b.a>=0){var cxp=(b.x1+b.x2)/2*sx,cyp=(b.y1+b.y2)/2*sy,"
        "th=b.a*Math.PI/180,L=Math.max((b.x2-b.x1)*sx,(b.y2-b.y1)*sy),"
        "dx=Math.cos(th)*L/2,dy=Math.sin(th)*L/2;"
        "g.strokeStyle='#f00';g.beginPath();g.moveTo(cxp-dx,cyp-dy);g.lineTo(cxp+dx,cyp+dy);g.stroke();"
        "g.strokeStyle='#0f0';g.fillText(b.a.toFixed(0),cxp+4,cyp-4);}}}"
        "function poll(){fetch('/detect').then(function(r){return r.json();}).then(draw).catch(function(){});}"
        "function dtog(){var b=document.getElementById('det');"
        "if(dt){clearInterval(dt);dt=null;b.textContent='识别开始';return;}"
        "dt=setInterval(poll,180);b.textContent='识别停止';}"
        "function atest(){fetch('/arm_test').then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('as').textContent=d.sent?'已发测试帧':('失败:'+d.err);});}"
        "function gset(){var g=document.getElementById('gr').value;"
        "fetch('/arm_grade?g='+g).then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('gs').textContent='grade='+d.grade;});}"
        "function arun(on){fetch('/arm_run?on='+on).then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('gs').textContent=d.running?'循环运行中':'已停止';});}"
        "document.getElementById('v').src='http://'+location.hostname+':81/stream';"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "{\"uptime_s\":%" PRIu32 ",\"heap_free\":%" PRIu32 ",\"psram_free\":%u}",
        (uint32_t)(esp_log_timestamp() / 1000U),
        (uint32_t)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 实时检测结果（JSON）。读 ai 缓存(生产者=main 的 detect_task)，不触发相机/推理，故 handler 轻。
static esp_err_t detect_get(httpd_req_t *req)
{
    ai_result_t r;
    ai_get_last(&r);
    char buf[1536];
    int n = snprintf(buf, sizeof(buf),
        "{\"w\":%d,\"h\":%d,\"infer_ms\":%u,\"n\":%d,\"boxes\":[",
        r.src_w, r.src_h, (unsigned)r.infer_ms, r.count);
    for (int i = 0; i < r.count && n < (int)sizeof(buf) - 128; i++) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"name\":\"%s\",\"s\":%.2f,\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"a\":%.1f,\"aniso\":%.2f}",
            i ? "," : "", ai_class_name(r.boxes[i].cls), r.boxes[i].score,
            r.boxes[i].x1, r.boxes[i].y1, r.boxes[i].x2, r.boxes[i].y2,
            r.boxes[i].angle_deg, r.boxes[i].anisotropy);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 机械臂目标（JSON）。读 armlink 缓存（生产者=detect_task），不触发相机/推理。
static esp_err_t arm_target_get(httpd_req_t *req)
{
    arm_target_t t;
    armlink_get_last_target(&t);
    char buf[256];
    int n;
    if (t.valid) {
        // wrist_deg 本步未标定，恒输出 null（标定后改真实值）
        n = snprintf(buf, sizeof(buf),
            "{\"valid\":true,\"cx\":%.1f,\"cy\":%.1f,\"angle_deg\":%.1f,\"score\":%.2f,"
            "\"wrist_deg\":null,\"w\":%u,\"h\":%u,\"frame_id\":%u}",
            t.center_x_px, t.center_y_px, t.angle_deg, t.score,
            (unsigned)t.src_w, (unsigned)t.src_h, (unsigned)t.frame_id);
    } else {
        n = snprintf(buf, sizeof(buf), "{\"valid\":false,\"frame_id\":%u}", (unsigned)t.frame_id);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 机械臂手动测试帧：点一下发一条固定安全 $KMS: 帧，供首次 UART 联调（受控、点一次发一次）。
static esp_err_t arm_test_get(httpd_req_t *req)
{
    esp_err_t e = armlink_send_test_frame();
    char buf[96];
    int n = snprintf(buf, sizeof(buf),
        "{\"sent\":%s,\"err\":\"%s\"}",
        (e == ESP_OK) ? "true" : "false", esp_err_to_name(e));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 自动发送开关：/arm_auto?on=1 开（检测到电池每帧自动发坐标驱动臂）；on=0 关。默认关。
static esp_err_t arm_auto_get(httpd_req_t *req)
{
    int on = -1;   // -1=仅查询不改
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 64) {
        char q[64], val[8];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
            on = (val[0] == '1') ? 1 : 0;
        }
    }
    if (on >= 0) armlink_set_auto_send(on != 0);
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"auto_send\":%s}",
                     armlink_get_auto_send() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 标定: GET 查询当前; POST body="H0,H1,...,H8" 写入并置 valid。
static esp_err_t arm_calib_get(httpd_req_t *req)
{
    armcal_t c;
    armcal_load(&c);

    if (req->method == HTTP_POST) {
        char body[256];
        int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
        int got = 0;
        while (got < total) {
            int r = httpd_req_recv(req, body + got, total - got);
            if (r <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
            got += r;
        }
        body[got] = '\0';
        float h[9];
        int nparsed = sscanf(body, "%f,%f,%f,%f,%f,%f,%f,%f,%f",
            &h[0],&h[1],&h[2],&h[3],&h[4],&h[5],&h[6],&h[7],&h[8]);
        if (nparsed != 9) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need 9 floats"); return ESP_FAIL; }
        for (int i = 0; i < 9; i++) c.H[i] = h[i];
        c.valid = true;
        esp_err_t e = armcal_save(&c);
        if (e == ESP_OK) armctrl_reload_cal();   // 免重启: 让运行中的 armctrl 空闲时重载新标定
        char ob[64];
        int n = snprintf(ob, sizeof(ob), "{\"saved\":%s}", e == ESP_OK ? "true" : "false");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, ob, n);
    }

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"valid\":%s,\"H\":[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f],"
        "\"observe\":[%.1f,%.1f,%.1f]}",
        c.valid ? "true" : "false",
        c.H[0],c.H[1],c.H[2],c.H[3],c.H[4],c.H[5],c.H[6],c.H[7],c.H[8],
        c.observe_x, c.observe_y, c.observe_z);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 安全级: /arm_grade?g=0..4 设置; 无参查询。
static esp_err_t arm_grade_get(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 32) {
        char q[32], val[4];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "g", val, sizeof(val)) == ESP_OK) {
            armctrl_set_grade(val[0] - '0');
        }
    }
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "{\"grade\":%d}", armctrl_get_grade());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 启停一轮抓取: /arm_run?on=1|0。
static esp_err_t arm_run_get(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 32) {
        char q[32], val[4];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
            armctrl_request_run(val[0] == '1');
        }
    }
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "{\"running\":%s}", armctrl_is_running() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t capture_get(httpd_req_t *req)
{
    // 解析 ?name=<类名>，只保留 [0-9A-Za-z_-]，缺省 "cap"
    char name[32] = "cap";
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 128) {
        char q[128];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            char val[32];
            if (httpd_query_key_value(q, "name", val, sizeof(val)) == ESP_OK && val[0]) {
                size_t j = 0;
                for (size_t i = 0; val[i] && j < sizeof(name) - 1; i++) {
                    char c = val[i];
                    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') || c == '_' || c == '-') {
                        name[j++] = c;
                    }
                }
                name[j] = '\0';
                if (j == 0) strcpy(name, "cap");
            }
        }
    }

    camera_fb_t *fb = camera_capture();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }

    static uint32_t seq = 0;
    char disp[96];
    snprintf(disp, sizeof(disp),
             "attachment; filename=\"%s_%" PRIu32 "_%" PRIu32 ".jpg\"",
             name, esp_log_timestamp(), seq++);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t r = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    camera_return(fb);   // 与 camera_capture 配对
    return r;
}

void net_http_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;   // 默认 4096 不够: detect_get 的 buf[1536]+ai_result_t 会撑爆 httpd 任务栈 → 卡死/崩溃
    config.max_uri_handlers = 16;   // 默认 8 不够: 已注册 11 个 URI(root/status/capture/detect/arm_*), 超出的会静默注册失败
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    httpd_uri_t root   = { .uri = "/",       .method = HTTP_GET, .handler = root_get };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    httpd_uri_t capture = { .uri = "/capture", .method = HTTP_GET, .handler = capture_get };
    httpd_register_uri_handler(server, &capture);
    httpd_uri_t detect = { .uri = "/detect", .method = HTTP_GET, .handler = detect_get };
    httpd_register_uri_handler(server, &detect);
    httpd_uri_t arm = { .uri = "/arm_target", .method = HTTP_GET, .handler = arm_target_get };
    httpd_register_uri_handler(server, &arm);
    httpd_uri_t arm_test = { .uri = "/arm_test", .method = HTTP_GET, .handler = arm_test_get };
    httpd_register_uri_handler(server, &arm_test);
    httpd_uri_t arm_auto = { .uri = "/arm_auto", .method = HTTP_GET, .handler = arm_auto_get };
    httpd_register_uri_handler(server, &arm_auto);
    httpd_uri_t calib_g = { .uri = "/arm_calib", .method = HTTP_GET,  .handler = arm_calib_get };
    httpd_register_uri_handler(server, &calib_g);
    httpd_uri_t calib_p = { .uri = "/arm_calib", .method = HTTP_POST, .handler = arm_calib_get };
    httpd_register_uri_handler(server, &calib_p);
    httpd_uri_t grade = { .uri = "/arm_grade", .method = HTTP_GET, .handler = arm_grade_get };
    httpd_register_uri_handler(server, &grade);
    httpd_uri_t run = { .uri = "/arm_run", .method = HTTP_GET, .handler = arm_run_get };
    httpd_register_uri_handler(server, &run);
    ESP_LOGI(TAG, "http server up -> http://192.168.4.1/");
}
