# 正式资源包目录分类索引

供以后人工核查客户端资源时按类别、路径和文件类型快速定位。以下目录与数量是对正式资源包的一次盘点快照；资源内容依据只取自：

`F:/9ymsgo/9you/机动战士敢达Online/GundamOnline/unpackdat/`

盘点时共见 **16 个顶层目录、253,163 个文件**。游戏版本或解包内容更新后，数量会变化；目录分类仍可作为定位入口。

各扩展名计数、包头和内部结构完成度见[正式资源包文件类型与结构审计](resource-format-structure-audit.md)。

## 顶层目录总览

| 顶层目录 | 文件数（本次快照） | 主要内容/常见格式 | 常用核查入口 |
|---|---:|---|---|
| `chara/` | 118,231 | 机体与角色资源；大量 `.mrb`、`.asd`、`.dds`、`.csv`、`.vra`、`.dxg` | `chara/<目录ID>/<资源ID>/{attr,model,tex}/`；目录ID到模型资源ID的关系先查正式配置表，不凭数字猜。 |
| `codex/` | 11 | `data/`、`tools/` 下少量辅助文件 | 属于包内工具/辅助内容，不要默认当作游戏运行资源。 |
| `font/` | 1 | 字体文件（`.ttc`） | 字体显示问题从这里核查。 |
| `fx/` | 10,776 | 特效；`.vra`、`.dxg`、`.dds`、`.png`、`.tga` | `fx/<效果族>/{attr,model,tex}/`；不少 PP 特效定义 `.vra` 直接引用图片或模型。 |
| `interface/` | 43,359 | UI 图片及布局；主要为 `.dds`，另有 `.xml`、`.csv` | `interface/{layout,icon,map,load,main,...}/`；布局定义和图片资源分开查。 |
| `item/` | 11,791 | 武器、装备及道具模型/贴图；`.vra`、`.dxg`、`.dds`、`.mrb` | `item/<类别>/<资源ID>/{attr,model,tex}/`；按 VRA `Geometry` 和材质贴图路径关联。 |
| `location/` | 1,004 | 地图路线/导航 XML | `location/<地图资源键>/*.xml`；检查地面、空中、水路等子文件及后缀变体。 |
| `material/` | 57 | 材质/着色器相关；`.mtrl`、`.hlsl`、`.sgd` | 全局材质、着色器定义；遇到渲染差异时再与模型材质绑定一起看。 |
| `movie/` | 3 | 视频文件（`.ogv`） | 过场/播放视频。 |
| `pilot/` | 1,102 | 驾驶员头像/图像资源；`.dds` | 按子目录和资源键查头像图。 |
| `scene/` | 261 | 场景资源；`.vra`、`.dxg`、`.mrb` | `scene/{common,mission,op,op3}/`；场景 VRA 可能继续关联模型、贴图和动作。 |
| `script/` | 9,810 | 客户端脚本；`.nut`、`.xml`、`.csv`、`.txt` | `script/{AI,ann,battle,camera,event,mission,system,UI}/`；用于核对脚本引用和表现流程，不直接等同服务端部署数据。 |
| `settings/` | 73 | 配置表；主要为 `.csv`，另有 `.txt`、`.cfg` | 地图、角色、资源映射等表；解释字段前先找实际消费者或权威配置说明。 |
| `sound/` | 25,363 | 音频；`.ogg` | `sound/{BGM,se,VOICE}/`。 |
| `tcpdump/` | 1 | 抓包文件（`.pcapng`） | 分析记录，不是客户端资源；使用前确认抓包范围和适用性。 |
| `world/` | 31,320 | 地图、建筑、环境资源；`.vra`、`.dxg`、`.dds`、`.mrb` | `world/{field,object,filter,sky}/`；地图场景、可复用物件、滤镜和天空分别查。 |

## 模型和场景资源的常见目录结构

| 资源类型 | 常见目录形态 | 入口文件 | 关联/检查顺序 |
|---|---|---|---|
| 机体/角色 | `chara/<目录ID>/<资源ID>/{attr,model,tex}/` | `.vra`、`.dxg`、`.mrb` | 先从正式配置或已证客户端关联确定资源ID；再看 VRA 的 Geometry、mesh/material、纹理和动作资源。 |
| 道具/武器 | `item/<类别>/<资源ID>/{attr,model,tex}/` | `.vra`、`.dxg` | 由 VRA 的 `Geometry` 字段确定 DXG；按 material id 查 `s_base` 等纹理引用。 |
| 地图场景 | `world/field/<地图键>/` | 场景 `.vra`，地形/碰撞 `.dxg` | 先查地图配置选择了哪个 field 资源；再区分 ground/object/area/collision 等文件用途。 |
| 可复用世界物件 | `world/object/<族ID>/{attr,model,tex}/` | `.vra`、`.dxg` | VRA→Geometry→mesh/material→贴图；地图是否引用该物件另查 field/场景 VRA。 |
| 特效 | `fx/<效果族>/{attr,model,tex}/`，另有 `fx/image/` | PP `.vra`、`.dxg`、图片 | 从效果 VRA 找 Sprite/Particle/Model 节点和直接资源路径；缺图时保留原路径并查正式文件是否存在。 |
| 演出场景 | `scene/<场景族>/` | `.vra`、`.dxg`、动作资源 | 逐层追场景定义中的资源引用；演出坐标和对象不自动代表战斗动态部署。 |

资源键的数字段和目录ID可能存在多种含义；请以配置、VRA 引用或客户端加载链确认，不要把目录编号直接解释成阵营、机体类别或玩法类型。

## 多来源交叉后的高置信分类

下表按“谁引用它、客户端把它送入哪条加载链、内容结构是什么”分类。目录名和文件名前缀仅作定位，不作为独立语义证据。

| 候选类别 | 强证据入口 | 客户端/文件结构证据 | 分类边界 |
|---|---|---|---|
| 地图选择与静态场景 | `settings/map.csv`、`settings/map_base.csv` | v15 `0x457770` 读取 map 行并构造 `world/field/`、`world/sky/` 路径；`map_id=100102` 明确列出地形、obj、2ndbox、sky、hit、area 模板等槽位。`*_grd.vra` 和普通对象 VRA 被回调分开处理。 | 可确定地图资源装配链；静态 VRA placement 不能推出网络实体/部署角色。 |
| 地图建筑与静态摆件 | 被 map/field VRA 实际引用的 `world/object/**/attr/*.vra` 和 `world/field/**` | field VRA placement 内有资源 `name/category`、位置、旋转、缩放、renderid/physics 等；引擎按 field/category/name 组装 attr 路径。DXG/VRA 网格和材质可进一步判定可见壳、隐藏碰撞代理或装饰。 | 只有静态摆放证据时就归为地图建筑/对象。`base/mainbase/stronghold` 字样不把它升级成在线基地模型；参见[基地证据分层索引](base-resource-index.md)。 |
| 动态角色模板与本地角色模型 | `settings/download_list.csv` 中 `CHARA_PARAM`、`CHARA_MODEL`；本地 `chara/<目录ID>/<资源ID>/` | 下载表区分运行时模板与本地模型资源。项目既有 IDA 证据闭合 `CHARA_PARAM.resource_id → CHARA_MODEL.model_pack_id → chara` 路径格式；本地 DXG/VRA 名族前缀能配对，但实际“哪台角色用哪个键”仍以模板值为准。 | VRA/DXG 文件存在不等于模板行存在、实体已部署或模型已被客户端创建；不要用地图建筑候选替代角色资源。 |
| 角色动作 | 角色目录内 `motionlist.csv`、`anim/*.mrb/.asd/.man`，部分目录有 `anim/motiontable.csv` | `motionlist.csv` 与 v15 loader 分支将普通/替代 MRB、ASD、MAN 作为不同动作资源输入；另一个 `motiontable.csv` 解析动作组/替代关系。MRB 有 v15 节点解析器和骨轨采样实现；ASD/MAN 的记录级格式也已由客户端消费者还原。 | 动作文件存在不等于现场已选择/播放；ASD参数枚举和MAN曲线目标属性仍需结合消费者上下文确认。两张 CSV 的列义不可混用。 |
| 道具/武器本地模型 | `settings/download_list.csv` 的 `ITEM`、`BULLET`、`WEAPON_SETTING` 等定义；本地 `item/**` | `item/<类别>/<资源ID>/{attr,model,tex}`；DXG 常与属性 VRA 成对，正式覆盖审计显示 2,918 个 item DXG 均找到配对 VRA。 | ITEM/武器定义、模型文件、战斗挂载/发射关系是不同层；文件名中 EM/EZ 配对不能单独证明实际装配。 |
| FX/粒子与效果模型 | `settings/fx.csv` 的类别+唯一ID→效果路径；本地 `fx/**` | v15 启动路径读取 `fx.csv` 前三列；PP VRA 有 `Target/Header/Plug/Object/Sprite/Particle` 层级并直接引用图片或模型。正式包 PP 样本可见 `.png/.dds/.tga` 直接路径。 | `fx.csv` 能分类资源目录键，不自动说明它绑定到哪个机体/状态。机体绑定必须再有状态处理调用证据。 |
| 演出/过场资源 | `scene/{common,mission,op,op3}/` 与 `script/mission/**`、`script/camera/**` | 场景脚本可显式创建本地角色/FX并控制镜头；VRA/DXG/MRB 作为场景输入。文件格式和脚本调用可用于把演出候选与场景资源交叉起来。 | 演出脚本里的角色键/坐标不自动是开局战斗部署表。 |
| 导航/路线 | `location/<地图资源键>/*.xml` | 官方树为按地图资源键组织的 XML；已有 v15 证据显示地图/BtlField 会选择带后缀的 way/airway/waterway 文件并可能回退默认项。 | 路线 XML 不等于地形、碰撞或动态单位。某个后缀文件缺失时不能从邻近地图随意补用。 |
| UI/图标/布局 | `interface/**`、`interface/layout/*.xml`、`settings/tex_list.txt`、相关 CSV | `tex_list.txt` 显式登记 texture 资源路径；layout XML 提供界面控件结构；正式设置表/IDA 消费者可将目录键关联到 UI 槽。 | UI 图标、场景对象、网络实体资源键彼此不同；相同数字或图像文字不代表同一个 ID 域。 |
| 音频 | `sound/{BGM,se,VOICE}/` 与 `settings/bgm.csv`、语音/音效配置 | OGG 路径和配置表中的资源键/语音条目可以交叉定位。 | 只见 OGG 文件不能证明触发条件、角色绑定或战斗时序。 |
| 材质/着色器 | `material/*.mtrl`、`.hlsl`、`.sgd`，VRA 材质参数 | VRA `material_set` 通过 `material_id` 绑定网格，纹理槽区分 `s_base/s_mix/s_gloss`；预览器已按该关系读取网格与纹理。 | 静态预览器尚未完全复现客户端 `.mtrl` 着色、FX 混合/UV 动画；贴图存在不能证明显示效果完全一致。 |
| 碰撞/遮挡/第二对象 | `map.csv` 对应的 `*_hit.dxg`、`*_2ndbox.vra`、`*_ocld.vra` 等路径 | map CSV 表头给出碰撞、第二对象/遮挡等槽；字段与 v15 地图行读取路径能交叉。DXG 文件头/块结构可校验这是可解析几何数据。 | 几何可解析不等于可见模型；`physics` 标志也尚不能单独说明完整的命中/走行行为。 |

## 包头与内部结构：只使用已经验证的判据

| 格式/结构 | 直接观察或实现证据 | 分类用途 | 未确认项 |
|---|---|---|---|
| DXG | 正式文件以字节 `DXG ` 开始；查看器审计读取偏移 20 的 name-table byte length，再从 24+名称表长度遍历 chunk type/size。样本含 chunk `1`；field DXG 还发现 `0x201` 使用兼容网格块结构。 | 用包头和块边界区分几何文件、坏块/截断文件；再读 DXG 内 mesh 名、骨骼/顶点记录，与 VRA 网格名和材质槽配对。 | 头部通过不代表所有 DXG 变体均完整支持；当前覆盖报告仍有异常文件，逐个查看 `viewer-asset-coverage.json`。 |
| VRA | 文本结构含 `{Version}` 与具名块；角色/普通物件常见 `geometry`、`material_set`，地图地形含 `Env/Models`，FX PP 含 `Target/Header/Plug/Object`。同一扩展名承载不同 schema。 | 优先按根级块和节点字段区分模型属性、地图场景、地形环境、FX 定义，不能看到 `.vra` 就当模型属性表。 | 版本号、未知块、编码和节点语义需按实际消费者分别确认。 |
| MRB | 正式动作样本文件头为 `MRB\0`；查看器 MRB parser 可取动作时长/骨名/采样并驱动已加载骨架。 | 可将 `anim/*.mrb` 作为动作预览候选。 | 未凭样本头推导所有字段布局；角色/动作绑定需另证。 |
| ASD | 无头的24字节定长时间事件记录数组；v15 `AE0BF0` 按时间窗口扫描、`AE0AE0` 按事件码分派。 | 可枚举动作关联的模型/音效/状态事件。 | 参数含义依事件类型而定，未知类型保持未命名。 |
| MAN | `0nam`容器；80字节官方样本含骨名字符串、曲线记录和两个(float,float)点；v15控制器按时间插值。 | 可查动作关联骨名及曲线值。 | 当前37份同属一种变体；曲线映射的宿主属性仍未知。 |

查看器全树资源审计当前记录：DXG 17,355 个，VRA 中普通纹理引用、FX Sprite/Particle 图片引用及模型配对分别统计在 `viewer-asset-coverage.json`。该报告适合发现结构异常和引用缺失；它不是游戏玩法语义词典。

## 常见文件类型速查

| 扩展名 | 常见位置 | 人工核查用途 |
|---|---|---|
| `.vra` | `chara/`、`item/`、`world/`、`fx/`、`scene/` | 属性/资源关联定义；查 Geometry、mesh、material、纹理、特效节点等引用。 |
| `.dxg` | 模型目录 | 几何模型；结合对应 VRA 查看网格、LOD、材质槽和隐藏/碰撞网格。 |
| `.dds` | `tex/`、`interface/`、`pilot/` | 纹理/界面图；核对资源路径、尺寸、alpha 和实际像素。 |
| `.png`、`.tga` | `fx/` 等 | 图片资源；按引用路径确认用途，不因同名就替换。 |
| `.mrb`、`.asd`、`.man` | 主要在 `chara/**/anim/` | 动画系统资源；MRB 节点/关键帧结构已解，ASD 事件数组和 MAN 曲线容器已由 v15 消费者还原到记录级。 |
| `.spr` | `fx/image/` | 小样本 FX 图片序列表；见结构审计，暂不泛化到未采样格式。 |
| `.xml` | `interface/`、`location/`、`script/` | UI 布局、路线/导航或脚本相关数据；以所在目录和消费者区分。 |
| `.csv`、`.cfg`、`.txt` | `settings/`、`chara/`、`script/` | 配置或辅助数据；区分官方包内容、开发/调试用途及资源映射。 |
| `.nut` | `script/` | Squirrel 客户端脚本；检查显式资源键和加载调用。 |
| `.ogg`、`.ogv`、`.ttc` | `sound/`、`movie/`、`font/` | 音频、视频和字体。 |
| `.mtrl`、`.hlsl`、`.sgd` | `material/` | 材质、着色器或相关编译资源；按客户端渲染链继续确认。 |

## 地图资源的特别提醒

- `world/field/<地图键>/` 是地图资源入口；`world/object/` 是可复用物件库，两者不要混为一个“地图清单”。
- `location/<地图资源键>/` 的导航 XML 与地图几何是不同资源层；地图文件存在不等于对应路线文件也存在。
- `settings/map.csv`、`settings/map_base.csv` 可帮助追地图资源选择和地图属性，但不包含所有战斗动态实体部署信息。
- Odessa 的基地模型专题索引见 [基地相关模型资源目录索引](base-resource-index.md)。该专题只覆盖基地候选，不是本总索引的全部范围。

## 人工核查记录模板

建议每次核查记录：`资源类别`、`正式资源键`、`完整相对路径`、`引用来源`、`实际关联文件`、`材质/贴图/动作依赖`、`文件存在性`、`查看器表现`、`客户端关联证据`、`结论`、`未确认项`。对缺失或歧义资源保留原始引用，不用同名猜测覆盖。
