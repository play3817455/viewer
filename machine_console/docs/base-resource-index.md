# 基地模型：证据分层索引

本页是基地专题核查页。文件名带 `base` 的资源只算检索命中，不能直接归为“基地模型”。正式资源只取自 `F:/9ymsgo/9you/机动战士敢达Online/GundamOnline/unpackdat/`；顶层目录和所有资源族的分类见[正式资源包目录分类索引](resource-directory-index.md)。

## 当前可下的分类结论

| 候选组 | 直接证据 | 当前分类 | 不能据此推出 |
|---|---|---|---|
| `world/object/40/attr/OZ_40_hl_mainbase01A.vra` → 其 `Geometry` 指向的 DXG | 正式地图场景 VRA 在 `F01_02_obj.vra` 等多处直接引用该资源；配对 DXG 有3个可渲染建筑网格，另有 `no_render=1` 的 `MeshBV`。客户端地图解析链把此类 world-field VRA placement 当本地对象读取属性和变换。 | **高置信：静态主基地建筑壳**。当前最可靠的可见基地外观预览入口。 | 不能推出它是网络实体、可受击核心或动态部署模板。静态场景记录没有网络 `ent_id`。 |
| `world/object/40/attr/OZ_40_hl_mainbase02A/03A/03B/04A/05A.vra` | 多张 field Area VRA 直接引用；`05A` 还出现在 `F01_17_area.vra` 以及其它地图 Area。 | **高置信：地图静态摆放的 mainbase 命名资源**；除已逐网格核实的 `01A` 外，其他变体的可见壳/代理网格需逐个检查。具体地图/版型是否加载要以 map/BtlField 资源选择为准。 | 不能把某个变体或某组摆放等同于特定任务的在线基地部署。 |
| `world/object/40/attr/OZ_40_wv_corep01B.vra` | 与壳体同位出现在多份 field VRA；VRA 关联正式 DXG。DXG 只有 `BBox01` 几何，未提供可见屋顶网格。 | **高置信：静态碰撞/包围代理候选**。查看器应标成隐藏代理，不当作可见建筑。 | 不能等同可受击网络核心；不能用它补出屋顶外观。 |
| `world/object/40` 的其它 `corep` 资源：`OZ_40_ko_corep01A/02A`、`OZ_40_wv_corep01A/01C` | 名称命中；各自 VRA→DXG 配对存在。尚未逐项完成地图引用与全部 DXG 网格角色审计。 | **待核查：corep 命名资源**。先查场景引用，再看是否隐藏网格。 | 名称不能证明是核心角色、屋顶件或玩法实体。 |
| `world/object/F19` 的 `base*`、`far_base*` | 对象 VRA 直接引用集中在 `F19_01_obj.vra`、`F19_02_obj.vra`；属于 F19 地图静态对象资源链。 | **高置信：F19 地图静态对象**；`far_base` 的远景/LOD角色目前只是名称提示，要看 VRA 引用和 DXG 内容确认。 | 不是“在线主基地模板”证据。组件组合和 LOD 含义仍应看具体 VRA/模型。 |
| `world/object/F21` 的 `airbase_mainbase*`、`colo_mainbase*`、`mainbase01*` | `F21_01_obj.vra`、`F21_02_obj.vra`、`F21_11_obj.vra` 有直接资源引用；同族另有 `airbase_obj*`、`wall*`、`subcol*`。 | **高置信：F21 地图静态对象组**。主体候选、墙体、附属件和 subcol 分开核查。 | 资源名中的 `mainbase` 不足以证明它属于动态强据点；每件是否可见需按 VRA/DXG 确认。 |
| `world/object/U11` 的 `base*` 模块 | `U11_00_obj.vra` 直接引用 `OZ_U11_base_01`；同族包含编号件和左右件。 | `base_01` 是**有场景引用的 U11 静态对象**；其他同族条目是模块化建筑候选，需检查是否被同一场景组装。 | 不能假定每个模块是独立完整基地；拼装规则要从场景引用确认。 |
| `world/object/05` 的 `stronghold*` | 正式 VRA→DXG 文件存在；本次精确关键字搜索未在 `world/field` 或 `settings` 找到直接引用。 | **未定候选**。优先查 script/scene/CHARA 配置或是否为未引用资产。 | `stronghold` 名称不能单独证明在线本拠点模型。 |
| `world/object/09`、`U13`、`U19`、`U21`、`U22`、`F18` 等含 `base` 的资源 | 搜索命中多为 `basewall`、`Basebridge`、`BASEengine/frame`、`grid_base`、`repairbase`、`baseobject` 等；必须按实际 field VRA 引用与模型内容逐项确认。 | **混合的环境建筑、结构件或语义待定资源**，不归入主基地类。 | 不因 `base` 子串就判为基地建筑或维修设施。 |
| `world/object/40/test_mainbase*`、`squle_base*`、`kn_base*`、`bstl_base*`、`th_mainbase_lightup` | 文件名和 VRA→DXG 配对存在；仅靠名字/目录不足以确定用途或目标地图。 | **实验、局部件或表现资源待核查**。在完整引用链前不纳入正式主基地清单。 | 名称不证明其是可部署基地、炮台或屋顶核心。 |

## 为什么地图建筑与在线基地必须分开

1. `settings/map.csv` 把地图资源拆成地形、主对象、第二对象、遮挡/天幕、碰撞、Area 等不同输入。以 `map_id=100102` 为例，记录直接选择 F01 地形、对象、二级对象、碰撞及 Area 模板。
2. v15 地图路径 `0x458730→0x458610→0x457770` 读取地图行；地图回调 `0xA83A70` 又把 `*_grd.vra` 与其它对象 VRA 分入不同处理路径。普通对象解析读取 `name/category/pos/rot/scale/renderid/physics` 并按 `world/field/<category>/attr/<name>.vra` 找本地对象属性。
3. 这条路径说明地图 VRA 是本地静态场景/资源输入；它没有因此变成网络动态实体。已证的地图机关/实体初始化另有 `mapgimmicks[]`、显式 `ent_id` 和模板查找链，不能拿静态建筑 VRA 替代。
4. `settings/download_list.csv` 将 `CHARA_PARAM`、`CHARA_MODEL`、`AI_PARAM`、`AI_WEAPON_PARAM` 等列为服务器下载数据表。动态战斗角色应从角色模板/实例身份/任务部署链核查，而不是从 `world/object/*` 的建筑名反推。

详细加载证据见 [v15 地图加载记录](client-map-loading-v15.md)、项目权威边界 `.cursor/rules/project-authorities.mdc` 和 [战场配置边界](../../../serverv/docs/R001-战场配置边界.md)。

## Odessa 的已核实静态资源入口

| 资源文件 | 当前可支持的结论 | 状态边界 |
|---|---|---|
| `world/field/F01/F01_02_obj.vra` | 四个 `OZ_40_hl_mainbase01A` 建筑壳位置；同位有四个 `OZ_40_wv_corep01B` 代理位置。 | 静态对象层；不是四个在线核心/角色实例。 |
| `world/field/F01/F01_16_area.vra` | 两个 mainbase/corep 同位模型位置候选。 | Area 资源选择受 BtlField 影响；是否是当前目标版型须按地图参数确认。 |
| `world/field/F01/F01_17_area.vra` | 四个 `OZ_40_hl_mainbase05A` 静态壳位置。 | 壳体变体证据；任务/在线部署绑定未证。 |

不要把其他 Area 文件中出现的同名模型自动并入 Odessa 某个任务。`map.csv` 能证明地图资源模板，具体任务/Area选择还须按客户端规则和任务键交叉确认。

## 原“151 组 base 搜索命中”的处理

151 只是 `world/object/**/attr/*.vra` 中名称含 `base/mainbase/corep/stronghold` 等子串、且 Geometry 文件存在的**检索集合**，不是基地资产总表。它混有地图建筑、远景、碰撞代理、零件、测试模型及语义未定资源。现在已按上表重分；未获得场景引用或角色/玩法链证据的条目留作待核候选，不能作为“基地模型”确认项。

要继续扩充可信度，请为每个候选至少补齐：`引用它的配置/场景文件`、`VRA Geometry 精确路径`、DXG 可见与隐藏网格、材质/纹理存在性，以及客户端加载路径归属（地图静态对象、角色模板、物品、FX 或演出）。只有静态场景引用时，分类停在“地图建筑/静态对象”。
