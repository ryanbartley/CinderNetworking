#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "Network.h"
#include <thread>

using namespace ci;
using namespace ci::app;
using namespace std;

std::mutex global_stream_lock;

class MyServerConnect : public Connection {
  private:
    void onAccept( const std::string & host, uint16_t port )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << host << ":" << port << std::endl;
        global_stream_lock.unlock();
        
        // Start the next receive
        recv();
    }
    
    void onConnect( const std::string & host, uint16_t port )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << host << ":" << port << std::endl;
        global_stream_lock.lock();
        
        recv();
    }
    
    void onSend( const std::vector< uint8_t > & buffer )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << buffer.size() << " bytes" << std::endl;
        for (size_t x = 0; x < buffer.size(); ++x) {
            std::cout << std::hex << std::setfill( '0' ) << std::setw( 2 ) << (int)buffer[ x ] << " ";
            if ( ( x + 1 ) % 16 == 0) {
                std::cout << std::endl;
            }
        }
        std::cout << std::endl;
        global_stream_lock.unlock();
    }
    
    void onRecv( std::vector< uint8_t > & buffer )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << buffer.size() << " bytes" << std::endl;
        for (size_t x = 0; x < buffer.size(); ++x) {
            std::cout << std::hex << std::setfill( '0' ) << std::setw( 2 ) << (int)buffer[ x ] << " ";
            if ( (x + 1 ) % 16 == 0) {
                std::cout << std::endl;
            }
        }
        std::cout << std::endl;
        global_stream_lock.unlock();
        
        // Start the next receive
        recv();
        
        // Echo the data back
        send( buffer );
    }
    
    void onTimer( const boost::posix_time::time_duration & delta )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << delta << std::endl;
        global_stream_lock.unlock();
    }
    
    void onError( const boost::system::error_code & ec )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << ec << std::endl;
        global_stream_lock.unlock();
    }
    
public:
    MyServerConnect( boost::shared_ptr< Hive > hive )
    : Connection( hive )
    {
    }
    
    ~MyServerConnect()
    {
    }
};

class MyServerAcceptor : public Acceptor {
private:
    bool onAccept( boost::shared_ptr< Connection > connection, const std::string & host, uint16_t port )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << host << ":" << port << std::endl;
        global_stream_lock.unlock();
        
        return true;
    }
    
    void onTimer( const boost::posix_time::time_duration & delta )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << delta << std::endl;
        global_stream_lock.unlock();
    }
    
    void onError( const boost::system::error_code & ec )
    {
        global_stream_lock.lock();
        std::cout << "[" << __FUNCTION__ << "] " << ec << std::endl;
        global_stream_lock.unlock();
    }
    
public:
    MyServerAcceptor( boost::shared_ptr< Hive > hive )
    : Acceptor( hive )
    {
    }
    
    ~MyServerAcceptor()
    {
    }
};

class Cinder_NetworkApp : public AppNative {
  public:
	void setup();
	void mouseDown( MouseEvent event );
    void keyDown( KeyEvent k );
	void update();
	void draw();
    
    boost::shared_ptr< Hive > hive;
    boost::shared_ptr< MyServerAcceptor > acceptor;
    boost::shared_ptr< MyServerConnect > connection;
};

void Cinder_NetworkApp::keyDown( KeyEvent k )
{
    hive->stop();
}

void Cinder_NetworkApp::setup()
{
    hive = boost::shared_ptr< Hive >( new Hive() );
    acceptor = boost::shared_ptr< MyServerAcceptor >( new MyServerAcceptor( hive ) );
    acceptor->listen("127.0.0.1", 7777);
    
    connection = boost::shared_ptr< MyServerConnect >( new MyServerConnect( hive ) );
    acceptor->accept( connection );
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
    
    hive->poll();
    sleep(1);
    
}

CINDER_APP_NATIVE( Cinder_NetworkApp, RendererGl )
