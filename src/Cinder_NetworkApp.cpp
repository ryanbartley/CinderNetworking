#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class Cinder_NetworkApp : public AppNative {
  public:
	void setup();
	void mouseDown( MouseEvent event );	
	void update();
	void draw();
};

void Cinder_NetworkApp::setup()
{
}

void Cinder_NetworkApp::mouseDown( MouseEvent event )
{
}

void Cinder_NetworkApp::update()
{
}

void Cinder_NetworkApp::draw()
{
	// clear out the window with black
	gl::clear( Color( 0, 0, 0 ) ); 
}

CINDER_APP_NATIVE( Cinder_NetworkApp, RendererGl )
