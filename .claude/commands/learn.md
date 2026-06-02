---
description: 把刚修好的 bug 沉淀为 CRASH_SIGNATURES.md 条目（或提炼新 skill）。
---
将刚解决的问题资产化（经验沉淀）：

1. 用 Edit 在 `docs/ai/CRASH_SIGNATURES.md` **顶部**追加一条，按既定格式：
   ```
   ### <短标题>  (今天日期)
   - 症状: <panic关键字/现象/回溯特征>
   - 根因: <真正原因>
   - 修法: <最小修改 + 文件:行>
   - 预防: <规则/检查/skill>
   - 标签: #...
   ```
2. 若这是**会反复出现的通用模式**，考虑提炼成新的 `.claude/skills/<name>/SKILL.md`，并在相关 `rules/*.md` 加一句指引。
3. 条目精炼、可检索；下次 triage 先查这里。
