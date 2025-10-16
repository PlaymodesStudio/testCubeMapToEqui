#include "ofApp.h"

// --- helpers ---------------------------------------------------------------

static glm::mat4 perspectiveFov(float fovDegrees, float aspect, float zNear, float zFar) {
	float f = 1.0f / tanf(glm::radians(fovDegrees) * 0.5f);
	glm::mat4 M(0.0f);
	M[0][0] = f / aspect;
	M[1][1] = f;
	M[2][2] = (zFar + zNear) / (zNear - zFar);
	M[2][3] = -1.0f;
	M[3][2] = (2.0f * zFar * zNear) / (zNear - zFar);
	return M;
}

static void makeCubeViews(const glm::vec3& pos, glm::mat4 V[6]) {
	// OpenGL RH, +X, -X, +Y, -Y, +Z, -Z, with conventional ups
	V[0] = glm::lookAt(pos, pos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)); // +X
	V[1] = glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)); // -X
	V[2] = glm::lookAt(pos, pos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)); // +Y
	V[3] = glm::lookAt(pos, pos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)); // -Y
	V[4] = glm::lookAt(pos, pos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)); // +Z
	V[5] = glm::lookAt(pos, pos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)); // -Z
}

static void checkFbo() {
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		ofLogError() << "FBO incomplete: 0x" << std::hex << status;
	}
}

// --- setup -----------------------------------------------------------------

void ofApp::setup() {
	ofDisableArbTex();
	ofSetVerticalSync(true);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // better sampling later
	
	// 1) Layered cubemap textures (color + depth)
	glGenTextures(1, &cubeColor);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeColor);
	for (int i=0;i<6;++i) {
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, 0, GL_RGBA16F,faceSize, faceSize, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	
	glGenTextures(1, &cubeDepth);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeDepth);
	for (int i=0;i<6;++i) {
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i, 0, GL_DEPTH_COMPONENT24,
					 faceSize, faceSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	
	// 2) Layered FBO (attach once!)
	glGenFramebuffers(1, &cubeFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, cubeFbo);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, cubeColor, 0); // layered
	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, cubeDepth, 0);  // layered
	GLenum drawBuf = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &drawBuf);
	checkFbo();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	// 3) 90° projection + face views
	proj90 = perspectiveFov(90.0f, 1.0f, 0.05f, 2000.0f);
	makeCubeViews(capturePos, view);
	
	// 4) Capture shader (VS+GS+FS)
	captureShader.setupShaderFromSource(GL_VERTEX_SHADER, R"GLSL(
  #version 410
  layout(location=0) in vec3 position;
  layout(location=2) in vec3 normal;    // <-- 2, not 1
  layout(location=3) in vec2 texcoord;  // <-- 3, not 2
  
  uniform mat4 uModel;
  
  out VS_OUT {
  vec3 worldPos;
  vec3 worldNrm;
  } v;
  
  void main() {
  vec4 wp = uModel * vec4(position, 1.0);
  v.worldPos = wp.xyz;
  v.worldNrm = mat3(uModel) * normal;
  gl_Position = wp;
  }
  )GLSL");
	
	captureShader.setupShaderFromSource(GL_GEOMETRY_SHADER, R"GLSL(
  #version 410
  layout(triangles, invocations = 6) in;
  layout(triangle_strip, max_vertices = 3) out;
  
  uniform mat4 uProj;
  uniform mat4 uView0;
  uniform mat4 uView1;
  uniform mat4 uView2;
  uniform mat4 uView3;
  uniform mat4 uView4;
  uniform mat4 uView5;
  
  in VS_OUT {
  vec3 worldPos;
  vec3 worldNrm;
  } v[];
  
  out GS_OUT {
  vec3 worldPos;
  vec3 worldNrm;
  vec3 viewDir;
  } g;
  
  mat4 viewForFace(int face) {
  if (face == 0) return uView0;
  if (face == 1) return uView1;
  if (face == 2) return uView2;
  if (face == 3) return uView3;
  if (face == 4) return uView4;
  return uView5;
  }
  
  void main() {
  int face = gl_InvocationID;        // 0..5
  mat4 V = viewForFace(face);
  
  for (int i = 0; i < 3; ++i) {
   vec4 wp = vec4(v[i].worldPos, 1.0);
   g.worldPos = v[i].worldPos;
   g.worldNrm = normalize(v[i].worldNrm);
   g.viewDir  = normalize(-v[i].worldPos); // camera at origin
   gl_Layer   = face;                       // write to this cube face
   gl_Position = uProj * V * wp;
   EmitVertex();
  }
  EndPrimitive();
  }
  )GLSL");
	
	captureShader.setupShaderFromSource(GL_FRAGMENT_SHADER, R"GLSL(
  #version 410
  in GS_OUT {
  vec3 worldPos;
  vec3 worldNrm;
  vec3 viewDir; // (not strictly needed, but fine to keep)
  } f;
  
  out vec4 oColor;
  
  // --- uniforms you set from C++ ---
  uniform vec3 uLightPos;   // omni light position (world)
  uniform vec3 uLightColor; // light color/intensity (e.g. 1,1,1)
  uniform vec3 uAmbient;    // ambient term (e.g. 0.04,0.04,0.04)
  uniform vec3 uBaseColor;  // material base color (e.g. 0.6,0.7,1.0)
  
  // Tunables
  const float kSpecPower    = 32.0;  // shininess
  const float kSpecStrength = 0.25;  // spec multiplier
  const float kFalloff      = 0.001; // inverse-square coef (tweak to taste)
  
  void main() {
  vec3 N = normalize(f.worldNrm);
  
  // Light vector and attenuation
  vec3  L   = uLightPos - f.worldPos;
  float d2  = max(dot(L, L), 1e-6);         // distance^2
  vec3  Ld  = L * inversesqrt(d2);          // normalize(L)
  float att = 1.0 / (1.0 + kFalloff * d2);  // soft inverse-square
  
  // View vector (camera is at origin for cubemap)
  vec3 V = normalize(-f.worldPos);
  
  // Diffuse
  float NdotL = max(dot(N, Ld), 0.0);
  vec3 diffuse = uBaseColor * NdotL;
  
  // Specular (Blinn)
  vec3  H   = normalize(Ld + V);
  float spec= pow(max(dot(N, H), 0.0), kSpecPower) * kSpecStrength;
  
  vec3 color = uAmbient * uBaseColor + (diffuse + spec) * uLightColor * att;
  oColor = vec4(color, 1.0);
  }
  )GLSL");
	
	captureShader.bindDefaults();
	captureShader.linkProgram();
	
	// 5) Equirect unwrap target (4096 x 2048) + shader
	ofFbo::Settings e;
	e.width  = faceSize*4;
	e.height = faceSize*2;
	e.internalformat = GL_RGBA16F;
	e.useDepth = false;
	e.textureTarget = GL_TEXTURE_2D;
	equirectFbo.allocate(e);
	
	equirectShader.setupShaderFromSource(GL_VERTEX_SHADER, R"GLSL(
  #version 150
  in vec4 position;
  in vec2 texcoord;
  out vec2 vUV;
  void main(){ vUV=texcoord; gl_Position=position; }
  )GLSL");
	
	equirectShader.setupShaderFromSource(GL_FRAGMENT_SHADER, R"GLSL(
  #version 150
  uniform samplerCube uCube;
  in vec2 vUV;
  out vec4 oColor;
  const float PI = 3.141592653589793;
  
  vec3 dirFromEquirect(vec2 uv){
   float theta = uv.x * 2.0*PI - PI;
   float phi   = uv.y * PI - (PI*0.5);
   float cp = cos(phi), sp = sin(phi);
   float ct = cos(theta), st = sin(theta);
   return normalize(vec3(cp*ct, sp, cp*st));
  }
  void main(){
   vec2 uv = clamp(vUV, vec2(1.0/4096.0, 1.0/2048.0),
   vec2(1.0) - vec2(1.0/4096.0, 1.0/2048.0));
   vec3 dir = dirFromEquirect(uv);
   vec3 c = texture(uCube, dir).rgb;
   oColor = vec4(c,1.0);
  }
  )GLSL");
	equirectShader.bindDefaults();
	equirectShader.linkProgram();
	
	// Fullscreen quad
	fsq.setMode(OF_PRIMITIVE_TRIANGLES);
	fsq.addVertex({-1,-1,0}); fsq.addTexCoord({0,0});
	fsq.addVertex({ 1,-1,0}); fsq.addTexCoord({1,0});
	fsq.addVertex({ 1, 1,0}); fsq.addTexCoord({1,1});
	fsq.addVertex({-1,-1,0}); fsq.addTexCoord({0,0});
	fsq.addVertex({ 1, 1,0}); fsq.addTexCoord({1,1});
	fsq.addVertex({-1, 1,0}); fsq.addTexCoord({0,1});
	
	
	// --- Test spheres -----------------------------------------------------------
	// A unit sphere we will scale per-instance (keeps GPU state simple)
	unitSphere.setRadius(1.0f);
	unitSphere.setResolution(3); // 2–3 keeps it light; increase if you want smoother
	
	// Deterministic RNG so runs are repeatable
	std::mt19937 rng(1337);
	std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
	std::uniform_real_distribution<float> rOrbit(60.0f, 180.0f);
	std::uniform_real_distribution<float> rRadius(6.0f, 22.0f);
	std::uniform_real_distribution<float> rSpeed(0.10f, 0.35f); // rad/s
	std::uniform_real_distribution<float> rPhase(0.0f, glm::two_pi<float>());
	
	const int kNumSpheres = 40;
	spheres.reserve(kNumSpheres);
	for (int i = 0; i < kNumSpheres; ++i) {
		glm::vec3 dir = glm::normalize(glm::vec3(uni(rng), uni(rng), uni(rng)));
		glm::vec3 axis = glm::normalize(glm::vec3(uni(rng), uni(rng), uni(rng)));
		if (!(glm::length2(axis) > 1e-6f)) axis = glm::vec3(0,1,0);
		
		SphereInst s;
		s.centerDir   = dir;            // base direction for the orbit path
		s.axis        = axis;           // orbit axis
		s.orbitRadius = rOrbit(rng);    // how far from origin it travels
		s.angularSpeed= rSpeed(rng);    // how fast it orbits
		s.phase       = rPhase(rng);    // starting angle
		s.radius      = rRadius(rng);   // sphere size
		spheres.push_back(s);
	}
	server.setName("equirect");
}

void ofApp::update() {
	// animate capture origin if you want; here we keep it at {0,0,0}
	makeCubeViews(capturePos, view);
}


void ofApp::draw() {
	// --- 1) Single-pass scene capture to layered cubemap ---------------------
	glBindFramebuffer(GL_FRAMEBUFFER, cubeFbo);
	glViewport(0,0,faceSize,faceSize);
	// Set background to RGB 20,10,5
	glClearColor(0.0f/255.0f, 50.0f/255.0f, 100.0f/255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	captureShader.begin();
	captureShader.setUniformMatrix4f("uProj", proj90);
	captureShader.setUniformMatrix4f("uView0", view[0]);
	captureShader.setUniformMatrix4f("uView1", view[1]);
	captureShader.setUniformMatrix4f("uView2", view[2]);
	captureShader.setUniformMatrix4f("uView3", view[3]);
	captureShader.setUniformMatrix4f("uView4", view[4]);
	captureShader.setUniformMatrix4f("uView5", view[5]);
	
	// Lighting/material
	captureShader.setUniform3f("uLightPos",  glm::vec3(0.0f, 0.0f, 0.0f)); // omni at origin
	captureShader.setUniform3f("uLightColor",glm::vec3(2.0f, 2.0f, 2.0f)); // white light
	captureShader.setUniform3f("uAmbient",   glm::vec3(0.04f, 0.04f, 0.04f));
	captureShader.setUniform3f("uBaseColor", glm::vec3(1.0f, 1.0f, 1.0f)); // tweak as you like
	
	drawScene();  // draw all meshes while the captureShader is bound
	captureShader.end();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	// --- 2) Unwrap to 4096x2048 equirect ------------------------------------
	equirectFbo.begin();
	ofClear(0,0,0,255);
	glViewport(0,0,equirectFbo.getWidth(), equirectFbo.getHeight());
	equirectShader.begin();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeColor);
	equirectShader.setUniform1i("uCube", 0);
	fsq.draw();
	equirectShader.end();
	equirectFbo.end();
	
	// --- 3) Preview ----------------------------------------------------------
	server.publishTexture(&equirectFbo.getTexture());
	
	ofBackground(12);
	float W = ofGetWidth(), H = ofGetHeight();
	float aspect = 2.0f; //always we need an AR of 2.0
	float drawW = W, drawH = W/aspect;
	if (drawH>H){ drawH=H; drawW=H*aspect; }
	equirectFbo.draw((W-drawW)*0.5f, (H-drawH)*0.5f, drawW, drawH);
}

// Draw your scene *using the capture shader* (it’s already bound).
void ofApp::drawScene() {
	glEnable(GL_DEPTH_TEST);
	
	drawTestSpheres(ofGetElapsedTimef());
	
	glDisable(GL_DEPTH_TEST);
}

void ofApp::drawTestSpheres(float t) {
	// Assumes captureShader is already bound and depth test enabled,
	// and that we'll set uModel per-instance.
	for (const auto& s : spheres) {
		float a = t * s.angularSpeed + s.phase;
		
		// Orbit: rotate the base direction around its axis, then scale by radius
		glm::mat4 R = glm::rotate(glm::mat4(1.0f), a, s.axis);
		glm::vec3 pos = glm::vec3(R * glm::vec4(s.centerDir * s.orbitRadius, 1.0f));
		
		// Model matrix = translate(pos) * uniform scale(s.radius)
		glm::mat4 M(1.0f);
		M = glm::translate(M, pos);
		M = glm::scale(M, glm::vec3(s.radius));
		
		captureShader.setUniformMatrix4f("uModel", M);
		unitSphere.getMesh().draw();
	}
}


void ofApp::keyPressed(int key){
	if(key=='s' || key=='S'){
		ofPixels px;
		equirectFbo.readToPixels(px);
		ofSaveImage(px, "equirect_" + ofGetTimestampString() + ".png");
		ofLogNotice() << "Saved equirect png file";
	}
}
