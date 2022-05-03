const dgram = require('dgram');
const DEFAULT_TIMEOUT = 5000;

class UdpComRes{
  constructor(recvPort, broadcast){
    this.broadcast = broadcast;
    this.requestMap = new Map();
    this.msgId = 0;
    this.socket = dgram.createSocket('udp4');

    this.socket.on('listening', () => {
      if( this.broadcast )
        this.socket.setBroadcast(true);
      const address = this.socket.address();
      console.log('UDP socket listening on ' + address.address + ":" + address.port);
    });
    this.socket.on('message', (message, remote) => {
      console.log("received:" + remote.address + ':' + remote.port +' - ' + message);
      var response = JSON.parse(message);
      if( this.requestMap.get(response.msgId) ){
        var obj = this.requestMap.get(response.msgId);
        obj.resolve(response.payload);
        this.requestMap.delete(response.msgId);
      }else{
        console.error('requestMap not found');
      }
      this.cleanUpRequestMap();
    });
    this.socket.on('error', (err) => {
      console.error(`UDP Server error: ${err.message}`);
    });
    this.socket.bind(recvPort);
  }

  cleanUpRequestMap(){
    var now = new Date().getTime();
    this.requestMap.forEach((value, key) => {
      if( value.create_at < now - DEFAULT_TIMEOUT ){
        value.reject("timeout");
        this.requestMap.delete(key);
        console.log("timeout deleted(" + key + ")");
      }
    });
  }
  
  transceive(message, port, host){
    this.cleanUpRequestMap();
    var msgId = ++this.msgId;
    return new Promise((resolve, reject) =>{
      this.requestMap.set(msgId, { resolve, reject, created_at: new Date().getTime() });
      var request = {
        msgId: msgId,
        payload: message
      };
      this.socket.send(JSON.stringify(request), port, host, (err, bytes) =>{
        if( err ){
          console.error(err);
          this.requestMap.delete(msgId);
          return reject(err);
        }
      });
    });
  }
}

module.exports = UdpComRes;