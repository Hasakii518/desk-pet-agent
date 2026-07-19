// ble_chunk.go —— BLE 分片与重组的纯逻辑（不依赖蓝牙栈，可单测）。
//
// 下行：一条「行 JSON」帧可能超过单包 ATT 负载（MTU-3），按 chunk 切开
// 逐个 WriteWithoutResponse；设备端按 \n 重组。上行：设备经 NUS TX
// notify 推字节流，bridge 这边同样按 \n 重组为行后回调 onLine。
//
// 分片不带序号/包头——行 JSON 协议自带 \n 定界，丢包只会坏一行，
// 设备解析失败的行直接丢弃（见 shared/protocol.md §6），无需可靠重组。
package transport

// ChunkFrame 把一帧（含末尾 \n）切成 ≤chunkSize 的片。chunkSize ≤ 0 时按 20。
// 返回 nil 表示空帧。
func ChunkFrame(frame []byte, chunkSize int) [][]byte {
	if len(frame) == 0 {
		return nil
	}
	if chunkSize <= 0 {
		chunkSize = 20
	}
	if len(frame) <= chunkSize {
		return [][]byte{frame}
	}
	out := make([][]byte, 0, (len(frame)+chunkSize-1)/chunkSize)
	for len(frame) > 0 {
		n := chunkSize
		if n > len(frame) {
			n = len(frame)
		}
		out = append(out, frame[:n])
		frame = frame[n:]
	}
	return out
}

// LineReassembler 把任意边界的字节流重组为 \n 分隔的行。
// 与 serial.go readLoop 的行切分逻辑一致：忽略 \r，超限行截断防内存膨胀。
type LineReassembler struct {
	buf   []byte
	max   int // 单行上限，0 → 1024
	Lines int64
}

// NewLineReassembler 构造重组器。maxLine 为单行最大字节数（超出丢弃多余部分）。
func NewLineReassembler(maxLine int) *LineReassembler {
	if maxLine <= 0 {
		maxLine = 1024
	}
	return &LineReassembler{max: maxLine}
}

// Feed 喂入一段字节，返回本轮凑齐的完整行（不含 \n）。
func (r *LineReassembler) Feed(data []byte) []string {
	var out []string
	for _, c := range data {
		switch c {
		case '\n':
			if len(r.buf) > 0 {
				out = append(out, string(r.buf))
				r.Lines++
			}
			r.buf = r.buf[:0]
		case '\r':
			// 忽略
		default:
			if len(r.buf) < r.max {
				r.buf = append(r.buf, c)
			}
		}
	}
	return out
}

// Reset 清空半行（断连时调用，避免下次连接拼出跨会话的怪行）。
func (r *LineReassembler) Reset() {
	r.buf = r.buf[:0]
}
