# ggZip 发布执行清单

发布门槛与恢复说明的权威定义见 [VALIDATION.md](VALIDATION.md)，本文件把执行步骤聚合成单一可勾选清单。
当前版本：`VERSION` = 0.4.0；正式标签应为 `v<VERSION>`（即 `v0.4.0`）。

**原则**：代码已实现 ≠ 自动化已验证 ≠ 正式发布。CI 成功不等于真实桌面验收完成。
本清单的任何步骤都不包含自动提交、推送、打标签、安装程序、修改默认应用或导入证书。

---

## 阶段 0：提交前准备

- [ ] 所有改动已提交，工作区干净：
      `packaging/preflight-release.ps1` 无阻塞项（`-AllowDirty` 仅用于本地演练）。
- [ ] 本地分支与 `origin/main` 同步，没有未推送提交。
- [ ] `VERSION` 已确定为要发布的版本，且 `v<VERSION>` 标签尚不存在。
- [ ] 新增功能已同步到 `README.md` 与 `ROADMAP.md`，版本说明一致。

```powershell
.\packaging\preflight-release.ps1
```

## 阶段 1：自动化门禁（当前提交的 CI 必须全绿）

- [ ] `ci.yml`：核心 CTest（含固定引擎 `GGZIP_7Z`）、FAT32/exFAT 隔离文件系统与磁盘满测试。
- [ ] `gui.yml`：GUI 构建、打包、安装/升级/卸载集成测试、界面解码回归。
- [ ] `package.yml`：**发布链路门禁**——CTest、
      **`test-filesystems.ps1`（FAT32/exFAT，2026-11-24 新增，release/nightly 均执行）**、
      Shell 宿主 E2E、安装器集成、有预算性能测量、产物 SHA256。
- [ ] `diagnostics` 审计通过：中英文原文键覆盖、稳定错误码/退出码不随语言变化。
- [ ] 跨卷 `cross_volume` 在有卷环境通过；单卷跳过不能当作成功。
- [ ] 多盘 ZIP 真实格式回归、分卷卷组识别与 MOTW 传播通过。
- [ ] 远端 CI 记录对应**当前待发布提交**，不用旧基线（`b64917d`）的成功记录替代。

## 阶段 2：真实桌面人工验收（隔离 Win10 1809+ / Win11 x64）

逐环境记录版本、场景与结果；不可把宿主测试或 `--selftest` 当成完整 GUI 交互。

- [ ] 安装、升级、卸载与便携启动；中文/空格路径；无需另装 7-Zip 或 Qt。
- [ ] 经典右键（含多选卷组、后续卷入口）、双击文件关联、已有默认应用与 `UserChoice` 保护。
- [ ] 压缩/解压/智能解压/选中解压、密码库明确保存、完整性测试、错误密码重试、关闭时取消。
- [ ] 栅格/文本/加密预览、系统打开默认取消、真实 Windows 下载来源安全警告。
- [ ] 批量队列：去重、失败继续/停止、单项/整队取消；ZIP/7z 事务编辑与分卷只读。
- [ ] 中文/英文 UI：文案、布局、截断、原生控件与中文/emoji 文件名。
- [ ] RAR `.partN.rar`/`.r00`、ZIP `.zNN`、数值卷的完整/缺卷/加密场景。
- [ ] 事务冲突：磁盘满、权限不足、被占用目标、异常中断、恢复目录按 `commit.txt` 处理。
- [ ] Win11 新式菜单：仅在可信签名后验证真实 Explorer、Inno 可选任务、入口去重、卸载清理。

## 阶段 3：发布资产核对

- [ ] 核对 `VERSION`、依赖、第三方许可（7-Zip LGPL/BSD/unRAR、Qt LGPL/GPL、libwebp）、
      对应源码（7-Zip 源码归档）、`BUILD_INFO.txt`、`SOURCE_INFO.txt`。
- [ ] 便携包包含 GUI、CLI、Shell DLL、预览辅助进程、Qt 插件（含 `qwebp`）、
      VC++ 运行库、内置引擎、SFX、源码与许可。
- [ ] 本地运行 `.\packaging\verify-artifacts.ps1`：SHA256 匹配、关键文件齐全、版本一致。
      旧产物（如 `out/packages/` 下的 11-23 包）会被正确拦截，应使用当前构建的新包。
- [ ] CI 中 `sha256sum --check *-SHA256SUMS.txt` 通过（与上述本地校验等价）。
- [ ] 签名方案已确定并在干净机器验证；未签名包仅作为 prerelease。
      签名钩子仅用当前用户证书库中已有证书，不入库私钥/PFX。

## 阶段 4：决策点（用户确认，不自动执行）

- [ ] 阶段 0～3 全部完成，向用户报告结果。
- [ ] **用户明确确认**后才提交、推送并创建 `v<VERSION>` 正式标签。
- [ ] 标签推送触发 `release.yml`：validate → package → publish（未签名测试版为 prerelease）。
- [ ] 发布后复查 Release 资产、SHA256 与 prerelease 标记；nightly 产物不受影响。

## 阶段 5：发布后

- [ ] 在发布页核对资产完整、校验和可用、说明与实际签名状态一致。
- [ ] 若后续升级内置 7-Zip：同步官方二进制、对应源码、许可与 `7zip.json` SHA256，
      并重跑完整内置引擎回归。

---

## 失败恢复要点（摘自 VALIDATION.md）

若程序报告恢复目录：停止写入原目标并保存路径，不删除目录；按 `commit.txt` 的
`OLD`/`NEW` 映射和 `rollback-*` 备份核对后恢复（分卷整组恢复），确认前保留备份。
当前**没有自动断电恢复承诺**，也不提供无确认的恢复命令。
