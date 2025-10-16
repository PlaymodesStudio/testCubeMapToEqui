#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){

//	//Use ofGLFWWindowSettings for more options like multi-monitor fullscreen
//	ofGLWindowSettings settings;
//	
//	settings.setSize(1024, 768);
//	settings.windowMode = OF_WINDOW; //can also be OF_FULLSCREEN
//
//	auto window = ofCreateWindow(settings);
//
//	ofRunApp(window, std::make_shared<ofApp>());
//	ofRunMainLoop();

	ofGLWindowSettings s;
	s.setGLVersion(4, 1);     // macOS GL 4.1 core
	s.setSize(2048, 1024);
	ofCreateWindow(s);
	ofRunApp(new ofApp());
}
