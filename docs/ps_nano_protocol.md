# PS—Nano协议V1开发约定

本文是基础外设协议；APP网关的0x60/0x62与APP_COMMAND扩展见[APP接口说明](app_guide.md)及[协议源码](../src/shared/vibproto/src/vp_frame.h)。当前两端以同一份shared/vibproto实现为准。

此协议是双方新程序共同实施的接口合同，不代表现有硬件已烧录实现。物理接口AX J11-19/20/21/22/23分别接Nano RX/TX/CTS/RTS/事件IRQ。先115200、8N1联调；当前100MHz UART16550高速档用781250、8N1、低有效RTS/CTS，双方统一切换并记录配置。此项更新不改变V1帧格式、命令或载荷。

只传控制、状态和少量事件，不传相机原始视频。载板SD由Nano管理，AX的主结果存储独立。校时、打印策略和测量ID由AX应用统一决定。

## 二进制帧格式

全部多字节整数为little-endian；JSON载荷UTF-8，无BOM、无结尾NUL。最大载荷1024字节、最大帧1036字节。Python参考编码器是测试向量的唯一字节级实现，不依赖额外库。

| 偏移 | 字节数 | 字段 | 含义 |
|---|---|---|---|
| 0 | 2 | SOF | 固定A5 5A |
| 2 | 1 | version | 1 |
| 3 | 1 | kind | 1请求、2响应、3事件 |
| 4 | 1 | opcode | 见命令表 |
| 5 | 1 | flags | V1固定0，未知非零拒绝 |
| 6 | 2 | seq | 请求1～65535，响应原样返回；事件0 |
| 8 | 2 | length | JSON载荷字节数，不是字符数 |
| 10 | length | payload | JSON对象，不能是数组/标量 |
| 10+length | 2 | CRC16 | little-endian；覆盖偏移2起8字节头＋全部payload |

CRC使用CRC-16/CCITT-FALSE：poly=0x1021、init=0xFFFF、refin=false、refout=false、xorout=0。ASCII字符串123456789的校验值0x29B1。SOF不参加校验。

接收是流解析，可遇到任意分包/粘包。先找A5 5A，长度超限、版本/flags/kind不合法或CRC错误时丢掉该候选头的第一个字节，再继续搜索。半帧超时起步200ms，清除该半帧后重同步；不能把短read当完整帧。计数CRC错、长度错、解析错及丢帧，不把二进制数据当行文本打印。

## 会话、响应和任务

AX是唯一请求发起方；Nano主动发送HELLO事件或IRQ提示。双方启动各生成boot_id，AX用HELLO取得Nano boot_id，并发送新的session_id。除HELLO外每个请求载荷都带session_id；Nano只接受当前会话。Nano复位或AX重连时重新握手，不能自动续用旧作业。

每次会话seq单调递增，回绕前等待所有请求完成，再换新session_id握手。Nano缓存最近32个请求的seq、opcode、完整载荷摘要和响应；AX最多8个请求未完成且同一请求最多尝试3次。ACK不明时重发必须复用同seq和相同载荷；同seq不同内容返回SEQ_CONFLICT。超过缓存窗口的请求不能重发为新作业，应查询状态。

普通响应载荷为session_id、ok、code及可选data。ok=true只表示该命令已按响应含义处理；PLAY_VOICE返回job_id表示已入队，实际结束由AUDIO_DONE事件或GET_STATUS作业状态表示。错误码至少包括BAD_ARGUMENT、BAD_SESSION、BUSY、NO_SD、NO_ASSET、POWER_FAIL、UNSUPPORTED、SEQ_CONFLICT、INTERNAL。响应opcode与seq必须等于原请求。

## 命令与载荷

| opcode | 请求名 | 请求特定字段 | 响应data / 行为 |
|---|---|---|---|
| 0x01 | HELLO | session_id，protocol=1，ax_version | nano_boot_id、nano_version、protocol、capabilities、event_cursor |
| 0x02 | GET_STATUS | 无 | uptime_ms、wifi、time_valid、sd、nfc、audio_job、power_fail、queue_overflow |
| 0x10 | SET_NET | ssid、password、persist(bool) | 接受配置；连接结果另发事件，日志不回显密码 |
| 0x11 | GET_TIME | 无 | unix_ms(UTC)、sample_uptime_ms、uncertainty_ms、valid、source |
| 0x20 | PLAY_VOICE | job_id、asset_id、volume(0～100) | 接受入队；同job_id重复提交不播放第二次 |
| 0x21 | STOP_VOICE | job_id | 取消指定作业；已完成时返回当前状态，不影响其他作业 |
| 0x30 | GET_SD_INFO | 无 | present、mounted、fs、total_bytes、free_bytes |
| 0x40 | DRAIN_EVENTS | after_event_id、limit(1～8) | 返回events数组及next_after、more；响应总载荷仍须≤1024 |
| 0x41 | ACK_EVENTS | nano_boot_id、through_event_id | 确认已处理的连续前缀，清除对应队列项 |
| 0x50 | PREPARE_POWER_OFF | reason | 禁止新音频/持久化任务，停止文件访问；不承诺电源保持时间 |

所有命令均须验证字段类型、范围及会话。SET_NET和持久化配置仅由AX配置界面明确提交，不在测量循环重复写入。音量先从小值验证，PLAY_VOICE只接收清单中的asset_id。

job_id由AX生成，字符串长度1～64。Nano在当前会话内记录至少最近32个已结束/活动音频job_id，重复提交返回该作业状态，不能再次播放。会话重置后不自动恢复播放，AX明确决定新作业。打印不走此协议，由AX本地打印服务管理。

## 事件与IRQ

事件kind=3、opcode=0x40、seq=0；payload有nano_boot_id、event_id、event_type、uptime_ms和data。事件类型包括NFC_TAG、NFC_REMOVED、WIFI_STATE、SD_STATE、AUDIO_STARTED、AUDIO_DONE、AUDIO_FAILED、TIME_SYNC、POWER_FAIL、FAULT。UID用大写十六进制字符串，保持字节顺序，并携带uid_len/type；不把UID转成本机整数导致前导零丢失。

事件在Nano产生时分配递增32bit event_id；同次Nano启动不复用，溢出前重建启动会话。AX可从主动事件接收，也可DRAIN补取；按nano_boot_id＋event_id去重。处理成功后才ACK连续前缀，不能越过尚未收到的事件。

Nano维护至少32项、每项编码后≤384字节的队列，未ACK的事件可重复发送；DRAIN只读取不删除。队列满时丢最旧事件并增加queue_overflow，保留最新状态，AX发现序号缺口或溢出后GET_STATUS重建状态并记录数据缺口。只要未ACK队列非空，D6 IRQ保持高，队列清空后拉低；它不是每事件一个脉冲。

IRQ采样和UART接收并行，AX不能看到IRQ低就丢弃已到的事件，也不能每个IRQ推断为一张NFC卡。ACK的through_event_id不得大于当前已产生ID；返回错误，不清空未知未来事件。

## 超时和重连

普通请求响应起步300ms，超时最多按同seq重发2次。网络连接/语音不是长时间阻塞响应：先返回接受，再用事件报告。音频完成超时按文件时长＋余量，SD/NFC错误分别报告。

连续请求超时先标记Nano离线、暂停外设业务并重新HELLO；必要时AX通过TCA P0.5拉高ESP_RESET再释放，复位脉宽按Nano复位要求和实测确定。不要因Wi-Fi断开就重置整个Nano。

掉电预警优先发POWER_FAIL并停止新作业，AX和Nano都不能等待一个保证不存在的完整关机窗口。恢复供电后重新握手和建立状态，禁止重发掉电前未确认的实际效果命令。

## 可运行的字节参考

运行python protocol_reference.py --self-test检验CRC已知向量、完整帧、随机切片、粘包、损坏帧恢复、长度超限与非法JSON。运行--vectors输出HELLO及响应等十六进制样例；静态样例另存[测试向量JSON](../src/shared/vibproto/reference/protocol_vectors.json)。脚本只编解码，不打开串口、不配置设备、不声称是完整ESP固件。

measurement_record.example.json表示主应用测量记录，和本协议的控制消息不同；示例数值均为null，避免误当实测或标定结果。
