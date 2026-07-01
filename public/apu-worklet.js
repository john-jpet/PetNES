/**
 * AudioWorklet processor: consumes Float32Array chunks posted from the main
 * thread and plays them back. Keeps at most ~0.25s buffered (drops oldest
 * when the emulator runs ahead); outputs silence on underrun.
 */
class ApuProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.chunks = [];
    this.offset = 0; // read offset into chunks[0]
    this.buffered = 0;
    this.port.onmessage = (e) => {
      this.chunks.push(e.data);
      this.buffered += e.data.length;
      while (this.buffered > 12000 && this.chunks.length > 1) {
        const dropped = this.chunks.shift();
        this.buffered -= dropped.length - this.offset;
        this.offset = 0;
      }
    };
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    let i = 0;
    while (i < out.length && this.chunks.length > 0) {
      const c = this.chunks[0];
      out[i++] = c[this.offset++];
      this.buffered--;
      if (this.offset >= c.length) {
        this.chunks.shift();
        this.offset = 0;
      }
    }
    for (; i < out.length; i++) out[i] = 0;
    for (let ch = 1; ch < outputs[0].length; ch++) outputs[0][ch].set(out);
    return true;
  }
}
registerProcessor("apu-processor", ApuProcessor);
