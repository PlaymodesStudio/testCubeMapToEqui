#pragma once
#include "ofMain.h"
#include "ofxSyphonServer.h"

class ofApp : public ofBaseApp {
public:
  void setup() override;
  void update() override;
  void draw() override;

  // Your scene drawing (uses the capture shader while bound)
  void drawScene();

  // Cubemap capture (single-pass layered)
  GLuint cubeColor = 0;     // GL_TEXTURE_CUBE_MAP (RGBA16F recommended)
  GLuint cubeDepth = 0;     // GL_TEXTURE_CUBE_MAP (DEPTH24)
  GLuint cubeFbo   = 0;     // layered FBO
  int    faceSize  = 1024;  // 4096x2048 equirect target

  ofShader captureShader;   // VS+GS+FS for the cubemap capture
  glm::mat4 proj90;         // 90° projection
  glm::mat4 view[6];        // 6 face views
  glm::vec3 capturePos = {0,0,0};

  // Equirect unwrap
  ofFbo    equirectFbo;
  ofShader equirectShader;
  ofMesh   fsq;

	// Add near the other members
	struct SphereInst {
	  glm::vec3 centerDir;   // unit direction from origin (orbit plane base)
	  glm::vec3 axis;        // orbit axis (unit)
	  float     orbitRadius; // distance from origin
	  float     angularSpeed;// radians per second
	  float     phase;       // initial angle
	  float     radius;      // sphere radius
	};

	std::vector<SphereInst> spheres;
	ofIcoSpherePrimitive    unitSphere;

	void drawTestSpheres(float t);
	void keyPressed(int key) override;
	
	ofxSyphonServer server;
};
