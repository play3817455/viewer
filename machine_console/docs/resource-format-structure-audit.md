# 正式资源包文件类型与结构审计

目标：把“扩展名是什么”与“文件实际装什么、谁引用它、能帮我们分析什么”分开记录。统计和样本只来自正式资源根 `F:/9ymsgo/9you/机动战士敢达Online/GundamOnline/unpackdat/`，本次递归盘点 **253,163 个文件、29 种扩展名**（含唯一的 `codex/README.md`）。文本/二进制结构按现有 v15 IDA 记录、C++ 解析器和样本头部交叉；“未解”明确保留，不按扩展名猜语义。

总目录分类见[正式资源包目录分类索引](resource-directory-index.md)；模型族与地图建筑/动态基地边界见[基地模型证据分层索引](base-resource-index.md)。

## 文件类型总表

| 扩展名 | 数量 | 头部/主要结构 | 分类把握 | 能帮助做什么分析 |
|---|---:|---|---|---|
| `.dxg` | 17,355 | 二进制 magic `DXG `；24字节固定头，offset 20 存名称表字节数，之后是带 type/size 的块。已知 chunk `1` 与地图 field 常见 `0x201` 网格体。内部含名称、网格/形状、顶点/索引及骨架相关数据。 | **较高**：有全树结构审计和查看器解码；仍有少数块界限/名称异常。 | 判断文件是否真是模型/碰撞几何；查 DXG 网格名→VRA mesh→material_id；分离可见 LOD、隐藏 BV、碰撞网格和坏块。名字和包头本身不说明玩法。 |
| `.vra` | 24,448 | 可读文本树；有 `{Version}`，但不同类型 schema 不同。角色/普通物件可见 `geometry/material_set`；地图地形可见 `Env/Models`；FX PP 可见 `Target/Header/Plug/Object/Sprite/Particle`；field placement 有 Group/对象变换和类别字段。 | **高（语法）/按节点语义分层**：多种 schema 已由解析器/IDA分别确认；未知键仍需逐类消费链。 | 最重要的本地引用图：Geometry、贴图、FX文件、位置/旋转/缩放、遮挡/物理提示。可从一个配置追到实际资源文件。 |
| `.mrb` | 97,441 | 已检查样本头为 `MRB\0`。v15 `sub_C38B90` 解析器模型：12字节流前缀（magic/version/node_count）；每节点44字节头含 node_type/field_mask，掩码控制变长字段块（count、stride、payload，4字节对齐）。node type 3 可还原轨道名、时间、位置/四元数/缩放样本与关键帧索引。 | **高（v15节点格式）**：有 IDA 对照的 C++ 结构解析器和动作采样；但应注意加载器分支差异。 | 动作ID/轨道名/关键帧能把角色姿态、骨架和动作资源串起来；可判断本地动作文件真实存在且结构有效。配合 `motionlist.csv` 与 `motiontable.csv` 查实际选择路径。 |
| `.asd` | 8,913 | 无文件头；全量均为24字节记录数组。每记录 `+0` float时间、`+4` DWORD事件类型、`+8..+20` 事件参数；负时间记录为终止项。 | **高（记录边界/分发）**：v15 `0xAE0C40→0xAE0BF0` 按24字节扫描时间区间，`0xAE0AE0` 按类型分发。8,913份均长度为24的倍数，且都以负时间项结束。 | 可按时间轴列出演出事件，分析动作关联的音效/模型事件；参数按事件类型解释，未知类型不能自行命名。 |
| `.man` | 37 | 80字节；`0nam`头。全量样本为版本1、单记录、记录偏移28、字符串表偏移64；记录有类型`0x00020001`、2个float对`(0,0),(1000,1或-4)`，字符串样本为`Caterpillar`。 | **高（现有官方样本变体）**：v15 `0xAAFC00` 检查头/版本/记录类型并构造控制器；`0xAAF8B0/0xAAFA80` 读取计数、字符串和float对，`0xAAFB50`按时间插值。保留+12字段及属性语义未知。 | 资源不是动作骨骼关键帧；是按骨名绑定、由客户端控制器插值消费的曲线输入。可辅助检查特定动作的绑定属性和时值。 |
| `.dds` | 64,330 | `DDS ` magic；标准 DDS 固定头，样本头含尺寸、像素格式/FourCC、mipmap 等字段。 | **高（容器头）**；具体格式还要解码 FourCC/DX10扩展和像素数据。 | 精确读取尺寸/格式、判断纹理能否被预览；用 VRA 的 `s_base/s_mix/s_gloss` 证实某贴图是否真被材质引用。 |
| `.png` | 2,132 | PNG 8字节签名；IHDR chunk 给出宽高/位深/颜色类型。 | **高（标准结构）**。 | FX 图片依赖和帧图；比较透明通道、分辨率与 VRA Sprite/Particle 引用。 |
| `.tga` | 127 | 无统一 magic；样本 TGA 头 image type 为2，头字段含宽高、像素深度。 | **中高（样本）**；需逐文件解析 image type、色表和 RLE。 | FX/角色纹理候选；可用引用链和 alpha 检查它是不是缺失 DDS 的真实目标。 |
| `.ogg` | 25,363 | Ogg page signature `OggS`；包内路径为 `sound/BGM`、`sound/se`、`sound/VOICE`。 | **高（容器/用途目录）**；codec 与 stream 粒度需解析 page packet。 | 从配置表、脚本和资源键追 BGM/音效/语音触发候选；单有音频文件不证明触发条件。 |
| `.ogv` | 3 | Ogg container，文件头同样为 `OggS`；路径 `movie/`。 | **高（容器/目录用途）**；视频 codec/时长应由媒体流解析确认。 | 过场视频候选，可与启动/场景脚本播放调用交叉。 |
| `.nut` | 7,055 | Squirrel 脚本文本；样本以 UTF-16LE BOM 开始，另有可见源码/日文注释。 | **高（脚本源码）**；个别编码需探测。 | 最有价值的语义证据之一：搜索 `createChrEvent`、模型/FX路径、任务阶段、坐标和注释。演出脚本与在线部署表必须区分。 |
| `.csv` | 2,118 | 逗号分隔文本，但表各自 schema 不同；常见日文注释、多行表头、`#` 注释和数据行混合。编码需按文件识别。 | **高（表文本）/逐表语义**。 | 建立数字ID到资源键的索引。优先查 `settings/download_list.csv`、`map.csv`、`fx.csv`、角色 `motionlist.csv`/`motiontable.csv`、关节 `MSJ_*.csv`。禁止把所有 CSV 套成同一个列定义。 |
| `.xml` | 3,739 | XML 文本；样本根节点包括 `Windows`、`PartsMap`、`ImgPartsList`；location 路线文件也在此类。 | **高（语法）/按目录根节点区分用途**。 | UI控件树/图片绑定、部件表和路线图数据；跨表路径可发现 UI资源、导航模板和地图子资源关系。 |
| `.txt` | 27 | 混合文本；settings 字符表/配置和 scene 脚本均有；样本有 UTF-16LE 与其他文本编码。 | **高（可读文本）/逐文件用途**。 | `settings/str_table.txt` 可定位客户端资源配置文件名和字符串键；演出 txt 可查脚本调用；先查 BOM/编码再读。 |
| `.cfg` | 2 | 纯文本设置行；如 `settings/config.cfg` 为 `key,value`，`autoexec.cfg` 是带注释的启动参数。 | **高（文本配置）**。 | 判断本地启动/渲染选项和客户端实际读取路径；某一配置项是否生效仍需对应 setter/消费者。 |
| `.spr` | 2 | FX 图片序列文本表。样本有逐行 `图片名<TAB>帧时长/数量`，另一种先写 `add` 再列 PNG 序列和数值。 | **中高（文件内容）**；仅2个样本，完整语法不保证覆盖其他版本。 | 可补充 Sprite 图集的帧序与时间参数，帮助区分动画序列与单张纹理；需要追 PP/客户端消费者确认。 |
| `.mtrl` | 28 | 类 VRA 的文本块；包含 `desc`、`platform/dx9/technique`、blend/depth等材质状态。 | **高（文本schema）/运行时编译消费部分未全闭合**。 | 把 VRA 材质参数与真实 DX9 technique/shader 状态关联，改善预览器对混合、深度、透明材质的还原。 |
| `.sgd` | 13 | 文本块；样本含 `semantics`、`vs_in`、`ps_in`、输出声明与顶点输入格式。 | **高（接口声明）**。 | 解释 DXG 顶点通道、shader 输入和 technique 匹配；可用于查缺少 UV/法线/权重导致的渲染错误。 |
| `.hlsl` | 16 | HLSL 着色器源码文本。 | **高（源码）**。 | 对照 `.sgd/.mtrl` technique 看 shader 的纹理槽、光照、混合和顶点语义。 |
| `.ttc` | 1 | TrueType Collection magic `ttcf`。 | **高（标准头）**。 | 字符显示/字体回退分析。 |
| `.pcapng` | 1 | pcapng section header magic `0A 0D 0D 0A`。 | **高（容器格式）**；样本是本地抓包记录，不是资源。 | 只能分析该抓包实际覆盖的网络行为；现有项目记录指出它不是战斗端口样本，不能补成战斗部署证据。 |
| `.json` | 2 | JSON文本。 | **高（标准格式）**；目录 `codex/` 的资料数据，不是客户端资源。 | 供包内附带的机体资料页面使用，不能替代官方服务端模板表。 |
| `.md` | 1 | Markdown文本；唯一文件为 `codex/README.md`。 | **高（文档）**；包内附加资料，不是客户端运行资源。 | 说明附带资料页/数据的用途；不作为引擎加载或玩法规则证据。 |
| `.js` | 2 | JavaScript 源码；位于 `codex/`。 | **高（源码）**；辅助网页应用，不是客户端逻辑。 | 了解包内附带资料页如何展示 JSON，不用于推断游戏加载规则。 |
| `.html` | 1 | HTML文档；位于 `codex/`。 | **高（源码）**；辅助页面，不是客户端资源。 | 仅帮助理解附带资料工具。 |
| `.css` | 1 | CSS文本；位于 `codex/`。 | **高（源码）**；辅助页面，不是客户端资源。 | 同上。 |
| `.py` | 3 | Python脚本；位于 `codex/tools/`，包含页面数据抽取/生成工具。 | **高（源码）**；包内附加工具，不是游戏逻辑。 | 可追溯 codex 资料生成方式；其数据来源不应混成正式游戏资源证据。 |
| `.bat` | 1 | Windows批处理文本；`codex/launch.bat`。 | **高（脚本）**；附加页面启动器。 | 不属于客户端资源加载链。 |
| `.bak_framing` | 1 | 261字节；内容像 CSV 表头/日文注释，路径为角色目录里的 `Camera_Hangar.csv.bak_framing`。 | **低：编辑/备份残留候选**，非已证运行资源。 | 可作为同目录 CSV 曾经编辑过的线索；不要把它当有效配置或资产。 |

## 对动画文件的补充结构和分析价值

不要把三类路径表混成一张：

- 角色资源目录下 `motionlist.csv` 由 v15 `0x571BF0→0x49D170` 读取；该路径把 CSV1–4 读为四个存在/模式标记、CSV5 读作浮点值。消费者分别走普通 MRB、替代 MRB、ASD、MAN 资源路径分支（`0xAE4700`、`0xAE4850`、`0xAE4980`、`0xAE4B30`）；后两者在该函数内只构造资源名并提交通用加载器，不能据此声称包体解析已闭环。现有记录没有证明 CSV3 是循环标志。
- `anim/motiontable.csv` 由另一条 `0x4A3C60→0xAE3790/0xAE3150/0x57B1F0` 读取，描述混合/全身/替代动作 ID 设置；不要套用 motionlist 的列义。
- `.asd` 和 `.man` 的包体布局已由 v15 消费者反汇编还原到记录层；它们不是未知杂项。事件/曲线参数的完整业务命名仍需结合全部动作上下文逐项确认。
- 形式上 MRB 有至少两个项目内解析入口：v15 通用文档解析器 `sub_C38B90` 的节点/field-mask结构，以及查看器现有 `MrbMotion` 播放器按关键帧数据段解码的实现（注释关联 `sub_B3FDB0`）。在同一批官方 MRB 上做字段/轨道/采样对照前，不假设两者已经覆盖完全相同的变体。

这类关联可以直接回答“某动作文件是不是这个角色/动作的合理候选、资源是否缺失、客户端选动作走哪张表”，比只按 `_数字.mrb` 文件名搜索更可靠。

## 最值得继续分析的包内表和引用链

| 优先级 | 文件/链 | 可以取得的信息 | 重要边界 |
|---|---|---|---|
| 1 | `settings/str_table.txt` → settings文件路径；`settings/download_list.csv` | 哪些配置表由客户端登记/请求；把本地静态资源与服务器下载的运行时模板分开。 | 下载表只登记表名/文件名，不包含缺失的原服数据行。 |
| 2 | `settings/map.csv` + v15 map row loader + `world/field/**` | map ID 到地形、对象、2ndbox、occluder、sky、hit、area、地图特效/滤镜的关系。 | 静态地图对象不等于 `mapgimmicks[]` 动态实体。 |
| 3 | `settings/fx.csv` + FX VRA 的 Sprite/Particle/Model `file` | 类别/唯一ID→效果路径→子贴图/模型资源依赖。 | 不能自动把 FX 目录键映射成某个机体状态字段。 |
| 4 | `CHARA_PARAM.resource_id → CHARA_MODEL.model_pack_id → chara` | 动态角色模板如何引用本地角色模型；设备/角色网格可与对应 attr/VRA 配对。 | 正式包缺失的服务器下载表行仍需服务端/网络证据补齐。 |
| 5 | `chara/**/motionlist.csv`、`motiontable.csv`、MRB/ASD/MAN | 动作号、动作模式标记、替代关系、文件存在性和骨骼轨道。 | 配置行可证明候选/加载选择，不能单独证明现场实际播放时序。 |
| 6 | `VRA→DXG mesh/material_id→material_set texture→DDS/PNG/TGA` | 精确重建模型和贴图依赖，定位资源缺失、错配和隐藏碰撞网格。 | 保持 VRA 原始引用；歧义贴图不能按同名猜配。 |
| 7 | `scene/**`、`script/mission/**`、`script/camera/**` 和 `.nut/.txt` | 任务演出、镜头、角色定义键、FX路径及对象局部坐标。 | 演出定义不等于在线开局部署表。 |
| 8 | `interface/layout/*.xml`、`parts.xml`、UI texture CSV | UI层级、图集/零件、图标资源键和显示槽。 | UI 键、世界资源键、网络实体 ID 是不同键域。 |

## 结构完成度和下一步

- 已形成较强结构判据：DXG、MRB、VRA、ASD、MAN、DDS/PNG/TGA、Ogg、XML、CSV、脚本文本、材质 HLSL/SGD/MTRL。
- ASD 的24字节事件记录与负时间结束项、MAN 的`0nam`头/控制器曲线记录已有 v15 消费者证据，并以正式资源全量尺寸与样本核对。ASD事件参数枚举和MAN曲线控制的目标属性仍未全部命名。
- ASD/MAN 的资源路径加载分支本身仍只是请求资源；格式结构来自后续真正消费函数 `0xAE0BF0/0xAE0AE0` 与 `0xAAFC00` 控制器链，而不是把加载函数误当解析器。
- 小样本格式需扩大样本：SPR（2个）、OGV（3个）、MAN（37个）；可先解析全部样本字段/引用，不应把少量样本泛化到版本外。
- 其余辅助/残留格式已按所在目录归类，不应投入引擎主资源优先级：codex 的 JSON/JS/HTML/CSS/PY/BAT，唯一 `.bak_framing`，以及 tcpdump 抓包。
- 全树 DXG/VRA/纹理关联质量数据见 `viewer-asset-coverage.json`。当前审计已有 DXG 坏块边界/名称异常、模型与 attr 配对、纹理引用未解析列表；应优先把异常链路与上表配置键做交叉，而不是再堆文件名关键词。

## 可复核依据

- `serverv/docs/R001-失败复测与客户端行为复核.md` 的 motionlist 复核段（约第645行）：记录 `0x571BF0→0x49D170→0x571A70` 与四个动作资源分支，并明确 CSV3 未证为循环标志。
- `serverv/docs/R001-战斗启动数据契约.md` 的 MRB 行走动作补证段（约第252行）：记录 `0xC38B90` 按字段掩码读 MRB 块，以及 `0xBFD150` 消费时间、S/R/T向量和骨骼关键帧索引。
- 本轮对指定 v15 IDB 的只读复核：`0xAE4980` 与 `0xAE4B30` 分别构造 `.asd`、`.man` 资源名并调用通用加载器；这只能支持“客户端请求对应资源”，不能单独支持 ASD/MAN 的二进制内部结构结论。
- ASD/MAN 的真实消费者与布局推导、样本核对见[ASD/MAN反汇编结构复核](asd-man-reverse-analysis.md)。
- 样本、计数及目录均来自正式资源根 `机动战士敢达Online/GundamOnline/unpackdat/`；项目权威边界见 `.cursor/rules/project-authorities.mdc`。表内标准格式头与“样本头”按证据范围分别表述，不把样本观察冒充全树签名校验。
