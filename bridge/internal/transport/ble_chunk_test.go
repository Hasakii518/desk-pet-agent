package transport

import (
	"strings"
	"testing"
)

func TestChunkFrameShort(t *testing.T) {
	chunks := ChunkFrame([]byte("{\"t\":\"heartbeat\"}\n"), 180)
	if len(chunks) != 1 {
		t.Fatalf("want 1 chunk, got %d", len(chunks))
	}
	if string(chunks[0]) != "{\"t\":\"heartbeat\"}\n" {
		t.Fatalf("chunk content mutated: %q", chunks[0])
	}
}

func TestChunkFrameSplits(t *testing.T) {
	frame := []byte(strings.Repeat("a", 500) + "\n")
	chunks := ChunkFrame(frame, 180)
	if len(chunks) != 3 { // 180+180+141
		t.Fatalf("want 3 chunks, got %d", len(chunks))
	}
	total := 0
	for i, c := range chunks {
		if len(c) > 180 {
			t.Fatalf("chunk %d too big: %d", i, len(c))
		}
		total += len(c)
	}
	if total != len(frame) {
		t.Fatalf("chunks lost bytes: %d != %d", total, len(frame))
	}
	// 拼接还原
	var sb strings.Builder
	for _, c := range chunks {
		sb.Write(c)
	}
	if sb.String() != string(frame) {
		t.Fatal("reassembled frame mismatch")
	}
}

func TestChunkFrameEmptyAndDefault(t *testing.T) {
	if ChunkFrame(nil, 180) != nil {
		t.Fatal("nil frame should give nil chunks")
	}
	chunks := ChunkFrame([]byte(strings.Repeat("x", 50)), 0)
	if len(chunks) != 3 { // 默认 20：20+20+10
		t.Fatalf("default chunk size 20 expected, got %d chunks", len(chunks))
	}
}

func TestReassemblerAcrossChunks(t *testing.T) {
	r := NewLineReassembler(0)
	// 一帧被切成 3 片喂入，只有最后一个 \n 后才出行
	line := "{\"t\":\"notify\",\"state\":\"building\"}"
	data := line + "\n"
	var got []string
	for i := 0; i < len(data); i += 7 {
		end := i + 7
		if end > len(data) {
			end = len(data)
		}
		got = append(got, r.Feed([]byte(data[i:end]))...)
	}
	if len(got) != 1 || got[0] != line {
		t.Fatalf("want [%q], got %v", line, got)
	}
}

func TestReassemblerMultipleLinesOneChunk(t *testing.T) {
	r := NewLineReassembler(0)
	got := r.Feed([]byte("a\nb\nc\n"))
	if len(got) != 3 || got[0] != "a" || got[2] != "c" {
		t.Fatalf("want 3 lines, got %v", got)
	}
	if r.Lines != 3 {
		t.Fatalf("Lines counter = %d, want 3", r.Lines)
	}
}

func TestReassemblerIgnoresCRAndEmptyLines(t *testing.T) {
	r := NewLineReassembler(0)
	got := r.Feed([]byte("\r\n\r\nhello\r\n\n"))
	if len(got) != 1 || got[0] != "hello" {
		t.Fatalf("CR/empty handling wrong: %v", got)
	}
}

func TestReassemblerTruncatesOversizedLine(t *testing.T) {
	r := NewLineReassembler(16)
	got := r.Feed([]byte(strings.Repeat("z", 100) + "\n"))
	if len(got) != 1 || len(got[0]) != 16 {
		t.Fatalf("want truncated 16-byte line, got %v", got)
	}
}

func TestReassemblerReset(t *testing.T) {
	r := NewLineReassembler(0)
	r.Feed([]byte("partial"))
	r.Reset()
	got := r.Feed([]byte("fresh\n"))
	if len(got) != 1 || got[0] != "fresh" {
		t.Fatalf("reset did not drop partial line: %v", got)
	}
}
