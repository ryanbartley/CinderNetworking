#include "network.h"
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/interprocess/detail/atomic.hpp>

//-----------------------------------------------------------------------------

Hive::Hive()
: mWorkPtr( new boost::asio::io_service::work( mIoService ) ), mShutdown( 0 )
{
}

Hive::~Hive()
{
}

boost::asio::io_service & Hive::getService()
{
	return mIoService;
}

bool Hive::hasStopped()
{
	return ( boost::interprocess::ipcdetail::atomic_cas32( &mShutdown, 1, 1 ) == 1 );
}

void Hive::poll()
{
	mIoService.poll();
}

void Hive::run()
{
	mIoService.run();
}

void Hive::stop()
{
	if( boost::interprocess::ipcdetail::atomic_cas32( &mShutdown, 1, 0 ) == 0 )
	{
		mWorkPtr.reset();
		mIoService.run();
		mIoService.stop();
	}
}

void Hive::reset()
{
	if( boost::interprocess::ipcdetail::atomic_cas32( &mShutdown, 0, 1 ) == 1 )
	{
		mIoService.reset();
		mWorkPtr.reset( new boost::asio::io_service::work( mIoService ) );
	}
}

//-----------------------------------------------------------------------------

Acceptor::Acceptor( boost::shared_ptr< Hive > hive )
: mHive( hive ), mAcceptor( hive->getService() ), mIoStrand(  hive->getService() ), mTimer( hive->getService() ),  mTimerInterval( 1000 ), mErrorState( 0 )
{
}

Acceptor::~Acceptor()
{
}

void Acceptor::startTimer()
{
	mLastTime = boost::posix_time::microsec_clock::local_time();
	mTimer.expires_from_now( boost::posix_time::milliseconds( mTimerInterval ) );
	mTimer.async_wait( mIoStrand.wrap( boost::bind( &Acceptor::handleTimer, shared_from_this(), _1 ) ) );
}

void Acceptor::startError( const boost::system::error_code & error )
{
	if( boost::interprocess::ipcdetail::atomic_cas32( &mErrorState, 1, 0 ) == 0 )
	{
		boost::system::error_code ec;
		mAcceptor.cancel( ec );
		mAcceptor.close( ec );
		mTimer.cancel( ec );
		onError( error );
	}
}

void Acceptor::dispatchAccept( boost::shared_ptr< Connection > connection )
{
	mAcceptor.async_accept( connection->getSocket(),  connection->getStrand().wrap( boost::bind(  &Acceptor::handleAccept, shared_from_this(), _1, connection ) ) );
}

void Acceptor::handleTimer( const boost::system::error_code & error )
{
	if( error || hasError() || mHive->hasStopped() )
	{
		startError( error );
	}
	else
	{
		onTimer( boost::posix_time::microsec_clock::local_time() - mLastTime );
		startTimer();
	}
}

void Acceptor::handleAccept( const boost::system::error_code & error, boost::shared_ptr< Connection > connection )
{
	if( error || hasError() || mHive->hasStopped() )
	{
		connection->startError( error );
	}
	else
	{
		if( connection->getSocket().is_open() )
		{
			connection->startTimer();
			if( onAccept( connection,  connection->getSocket().remote_endpoint().address().to_string(),  connection->getSocket().remote_endpoint().port() ) )
			{
				connection->onAccept(  mAcceptor.local_endpoint().address().to_string(),  mAcceptor.local_endpoint().port() );
			}
		}
		else
		{
			startError( error );
		}
	}
}

void Acceptor::stop()
{
	mIoStrand.post( boost::bind( &Acceptor::handleTimer, shared_from_this(), boost::asio::error::connection_reset ) );
}

void Acceptor::accept( boost::shared_ptr< Connection > connection )
{
	mIoStrand.post( boost::bind( &Acceptor::dispatchAccept, shared_from_this(), connection ) );
}

void Acceptor::listen( const std::string & host, const uint16_t & port )
{
	boost::asio::ip::tcp::resolver resolver( mHive->getService() );
	boost::asio::ip::tcp::resolver::query query( host, boost::lexical_cast< std::string >( port ) );
	boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve( query );
	mAcceptor.open( endpoint.protocol() );
	mAcceptor.set_option( boost::asio::ip::tcp::acceptor::reuse_address( false ) );
	mAcceptor.bind( endpoint );
	mAcceptor.listen( boost::asio::socket_base::max_connections );
	startTimer();
}

boost::shared_ptr< Hive > Acceptor::getHive()
{
	return mHive;
}

boost::asio::ip::tcp::acceptor & Acceptor::getAcceptor()
{
	return mAcceptor;
}

int32_t Acceptor::getTimerInterval() const
{
	return mTimerInterval;
}

void Acceptor::setTimerInterval( int32_t timerInterval )
{
	mTimerInterval = timerInterval;
}

bool Acceptor::hasError()
{
	return ( boost::interprocess::ipcdetail::atomic_cas32( &mErrorState, 1, 1 ) == 1 );
}

//-----------------------------------------------------------------------------

Connection::Connection( boost::shared_ptr< Hive > hive )
: mHive( hive ), mSocket( hive->getService() ), mIoStrand(  hive->getService() ), mTimer( hive->getService() ),  mReceiveBufferSize( 4096 ), mTimerInterval( 1000 ), mErrorState( 0  )
{
}

Connection::~Connection()
{
}

void Connection::bind( const std::string & ip, uint16_t port )
{
	boost::asio::ip::tcp::endpoint endpoint( boost::asio::ip::address::from_string( ip ), port );
	mSocket.open( endpoint.protocol() );
	mSocket.set_option( boost::asio::ip::tcp::acceptor::reuse_address( false ) );
	mSocket.bind( endpoint );
}

void Connection::startSend()
{
	if( !mPendingSends.empty() )
	{
		boost::asio::async_write( mSocket, boost::asio::buffer(  mPendingSends.front() ), mIoStrand.wrap( boost::bind(  &Connection::handleSend, shared_from_this(),  boost::asio::placeholders::error, mPendingSends.begin() ) ) );
	}
}

void Connection::startRecv( int32_t totalBytes )
{
	if( totalBytes > 0 )
	{
		mRecvBuffer.resize( totalBytes );
		boost::asio::async_read( mSocket, boost::asio::buffer(  mRecvBuffer ), mIoStrand.wrap( boost::bind(  &Connection::handleRecv, shared_from_this(), _1, _2 ) ) );
	}
	else
	{
		mRecvBuffer.resize( mReceiveBufferSize );
		mSocket.async_read_some( boost::asio::buffer( mRecvBuffer ),  mIoStrand.wrap( boost::bind( &Connection::handleRecv,  shared_from_this(), _1, _2 ) ) );
	}
}

void Connection::startTimer()
{
	mLastTime = boost::posix_time::microsec_clock::local_time();
	mTimer.expires_from_now( boost::posix_time::milliseconds( mTimerInterval ) );
	mTimer.async_wait( mIoStrand.wrap( boost::bind( &Connection::dispatchTimer, shared_from_this(), _1 ) ) );
}

void Connection::startError( const boost::system::error_code & error )
{
	if( boost::interprocess::ipcdetail::atomic_cas32( &mErrorState, 1, 0 ) == 0 )
	{
		boost::system::error_code ec;
		mSocket.shutdown( boost::asio::ip::tcp::socket::shutdown_both, ec );
		mSocket.close( ec );
		mTimer.cancel( ec );
		onError( error );
	}
}

void Connection::handleConnect( const boost::system::error_code & error )
{
	if( error || hasError() || mHive->hasStopped() )
	{
		startError( error );
	}
	else
	{
		if( mSocket.is_open() )
		{
			onConnect( mSocket.remote_endpoint().address().to_string(), mSocket.remote_endpoint().port() );
		}
		else
		{
			startError( error );
		}
	}
}

void Connection::handleSend( const boost::system::error_code &  error, std::list< std::vector< uint8_t > >::iterator itr )
{
	if( error || hasError() || mHive->hasStopped() )
	{
		startError( error );
	}
	else
	{
		onSend( *itr );
		mPendingSends.erase( itr );
		startSend();
	}
}

void Connection::handleRecv( const boost::system::error_code & error, int32_t actual_bytes )
{
	if( error || hasError() || mHive->hasStopped() )
	{
		startError( error );
	}
	else
	{
		mRecvBuffer.resize( actual_bytes );
		onRecv( mRecvBuffer );
		mPendingRecvs.pop_front();
		if( !mPendingRecvs.empty() )
		{
			startRecv( mPendingRecvs.front() );
		}
	}
}

void Connection::handleTimer( const boost::system::error_code & error )
{
	if( error || hasError() || mHive->hasStopped() )
	{
		startError( error );
	}
	else
	{
		onTimer( boost::posix_time::microsec_clock::local_time() - mLastTime );
		startTimer();
	}
}

void Connection::dispatchSend( std::vector< uint8_t > buffer )
{
	bool shouldStartSend = mPendingSends.empty();
	mPendingSends.push_back( buffer );
	if( shouldStartSend )
	{
		startSend();
	}
}

void Connection::dispatchRecv( int32_t totalBytes )
{
	bool shouldStartReceive = mPendingRecvs.empty();
	mPendingRecvs.push_back( totalBytes );
	if( shouldStartReceive )
	{
		startRecv( totalBytes );
	}
}

void Connection::dispatchTimer( const boost::system::error_code & error )
{
	mIoStrand.post( boost::bind( &Connection::handleTimer, shared_from_this(), error ) );
}

void Connection::connect( const std::string & host, uint16_t port)
{
	boost::system::error_code ec;
	boost::asio::ip::tcp::resolver resolver( mHive->getService() );
	boost::asio::ip::tcp::resolver::query query( host, boost::lexical_cast< std::string >( port ) );
	boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve( query );
	mSocket.async_connect( *iterator, mIoStrand.wrap( boost::bind(  &Connection::handleConnect, shared_from_this(), _1 ) ) );
	startTimer();
}

void Connection::disconnect()
{
	mIoStrand.post( boost::bind( &Connection::handleTimer, shared_from_this(), boost::asio::error::connection_reset ) );
}

void Connection::recv( int32_t totalBytes )
{
	mIoStrand.post( boost::bind( &Connection::dispatchRecv, shared_from_this(), totalBytes ) );
}

void Connection::send( const std::vector< uint8_t > & buffer )
{
	mIoStrand.post( boost::bind( &Connection::dispatchSend, shared_from_this(), buffer ) );
}

boost::asio::ip::tcp::socket & Connection::getSocket()
{
	return mSocket;
}

boost::asio::strand & Connection::getStrand()
{
	return mIoStrand;
}

boost::shared_ptr< Hive > Connection::getHive()
{
	return mHive;
}

void Connection::setReceiveBufferSize( int32_t size )
{
	mReceiveBufferSize = size;
}

int32_t Connection::getReceiveBufferSize() const
{
	return mReceiveBufferSize;
}

int32_t Connection::getTimerInterval() const
{
	return mTimerInterval;
}

void Connection::setTimerInterval( int32_t timerInterval )
{
	mTimerInterval = timerInterval;
}

bool Connection::hasError()
{
	return ( boost::interprocess::ipcdetail::atomic_cas32( &mErrorState, 1, 1 ) == 1 );
}
