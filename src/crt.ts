/** WebGL CRT post-process renderer. Renders the NES framebuffer to a canvas
 *  with optional barrel distortion, scanlines, phosphor bloom and vignette. */

const VS = /* glsl */`
attribute vec2 aPos;
varying vec2 vUv;
void main() {
  vUv = aPos * 0.5 + 0.5;
  vUv.y = 1.0 - vUv.y;
  gl_Position = vec4(aPos, 0.0, 1.0);
}`;

const FS = /* glsl */`
precision mediump float;
uniform sampler2D uTex;
uniform vec2      uScreen;  // canvas width/height in physical pixels
uniform float     uCrt;     // 0 = bypass, 1 = full effect

varying vec2 vUv;

vec2 barrel(vec2 uv) {
  uv -= 0.5;
  float r2 = dot(uv, uv);
  uv *= 1.0 + r2 * 0.20;
  return uv + 0.5;
}

void main() {
  vec2 uv = uCrt > 0.5 ? barrel(vUv) : vUv;

  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec3 col = texture2D(uTex, uv).rgb;

  if (uCrt > 0.5) {
    // Scanlines: at 3x NES scale every 3rd screen row is a dim gap
    float row  = floor(uv.y * uScreen.y);
    float scan = mod(row, 3.0) < 2.0 ? 1.0 : 0.22;
    col *= scan;

    // Phosphor bloom — cheap 4-tap using texel-space offsets
    vec2 d = 1.0 / vec2(256.0, 240.0);
    vec3 bloom = (
      texture2D(uTex, uv + d * vec2( 1.0,  0.0)).rgb +
      texture2D(uTex, uv + d * vec2(-1.0,  0.0)).rgb +
      texture2D(uTex, uv + d * vec2( 0.0,  1.0)).rgb +
      texture2D(uTex, uv + d * vec2( 0.0, -1.0)).rgb
    ) * 0.045 * scan;
    col += bloom;

    // Vignette
    vec2 vig = uv * (1.0 - uv.yx);
    col     *= pow(vig.x * vig.y * 18.0, 0.28);
  }

  gl_FragColor = vec4(col, 1.0);
}`;

function compile(gl: WebGLRenderingContext, type: number, src: string): WebGLShader {
  const s = gl.createShader(type)!;
  gl.shaderSource(s, src);
  gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
    throw new Error("Shader error: " + gl.getShaderInfoLog(s));
  return s;
}

export class CrtRenderer {
  private gl:      WebGLRenderingContext;
  private prog:    WebGLProgram;
  private tex:     WebGLTexture;
  private quad:    WebGLBuffer;
  private uTex:    WebGLUniformLocation;
  private uScreen: WebGLUniformLocation;
  private uCrt:    WebGLUniformLocation;

  crtEnabled = true;

  constructor(private canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl", {
      antialias:            false,
      preserveDrawingBuffer: true,  // needed for screenshots
    });
    if (!gl) throw new Error("WebGL not supported");
    this.gl = gl;

    // Program
    const prog = gl.createProgram()!;
    gl.attachShader(prog, compile(gl, gl.VERTEX_SHADER,   VS));
    gl.attachShader(prog, compile(gl, gl.FRAGMENT_SHADER, FS));
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS))
      throw new Error("Program link error: " + gl.getProgramInfoLog(prog));
    this.prog = prog;

    // Fullscreen quad
    this.quad = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
      -1,-1,  1,-1, -1, 1,
       1,-1,  1, 1, -1, 1,
    ]), gl.STATIC_DRAW);

    // Texture
    this.tex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    this.uTex    = gl.getUniformLocation(prog, "uTex")!;
    this.uScreen = gl.getUniformLocation(prog, "uScreen")!;
    this.uCrt    = gl.getUniformLocation(prog, "uCrt")!;
  }

  /** Upload a new 256×240 RGBA framebuffer. */
  upload(pixels: Uint8Array): void {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 256, 240, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
  }

  /** Draw the frame to the canvas. */
  render(): void {
    const gl = this.gl;
    const w  = this.canvas.width;
    const h  = this.canvas.height;
    gl.viewport(0, 0, w, h);
    gl.useProgram(this.prog);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.uniform1i(this.uTex, 0);
    gl.uniform2f(this.uScreen, w, h);
    gl.uniform1f(this.uCrt, this.crtEnabled ? 1.0 : 0.0);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    const aPos = gl.getAttribLocation(this.prog, "aPos");
    gl.enableVertexAttribArray(aPos);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }
}
