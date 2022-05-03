'use strict';

const HELPER_BASE = process.env.HELPER_BASE || "/opt/";
const Response = require(HELPER_BASE + 'response');

const UDP_DEVICE_ADDRESS = '192.168.1.255';
const UDP_DEVICE_PORT = 3311;
const UDP_RECV_PORT = 3312;
const UDP_REPORTSTATE_PORT = 3314;
const JWT_FILE_PATH = './keys/[クレデンシャルファイル名]';

const jwt_decode = require('jwt-decode');
const {smarthome} = require('actions-on-google');

const jwt = require(JWT_FILE_PATH);
const app = smarthome({
  jwt: jwt
});

const dgram = require('dgram');
const UdpComRes = require('./UdpComRes');
var udpcomres = new UdpComRes(UDP_RECV_PORT, true);

var requestId = 0;

var agentUserId = process.env.DEFAULT_USER_ID || "user01";

const onsync = require('./onsync.json');

app.onSync((body, headers) => {
  console.info('onSync');
  console.log('onSync body', body);
  console.log(headers);

//  var decoded = jwt_decode(headers.authorization);
//  console.log(decoded);

  var result = {
    requestId: body.requestId,
    payload: {
      agentUserId: agentUserId,
      devices: []
    }
  };

  onsync.forEach((item) =>{
    result.payload.devices.push(item);
  });
  console.log(JSON.stringify(result, null, '\t'));

  console.log("onSync result", result);
  return result;
});

app.onQuery(async (body, headers) => {
  console.info('onQuery');
  console.log('onQuery body', body);

//  var decoded = jwt_decode(headers.authorization);
//  console.log(decoded);

  const {requestId} = body;
  const payload = {
    devices: {}
  };

  for( var i = 0 ; i < body.inputs.length ; i++ ){
    if( body.inputs[i].intent == 'action.devices.QUERY' ){
      for( var j = 0 ; j < body.inputs[i].payload.devices.length ; j++ ){
        var device = body.inputs[i].payload.devices[j];
        var params = {
          intent: 'action.devices.QUERY',
          device_id: device.id
        };
        try{
          var message = await udpcomres.transceive(params, UDP_DEVICE_PORT, UDP_DEVICE_ADDRESS);
          payload.devices[device.id] = message.states;
          payload.devices[device.id].status = "SUCCESS";
        }catch(error){
          console.error(error);
        }
      }
    }
  }

  var result = {
    requestId: requestId,
    payload: payload,
  };

  console.log("onQuery result", JSON.stringify(result));
  return result;
});

app.onExecute(async (body, headers) => {
  console.info('onExecute');
  console.log('onExecute body', JSON.stringify(body));

//  var decoded = jwt_decode(headers.authorization);
//  console.log(decoded);
  
  const {requestId} = body;

  // Execution results are grouped by status
  var ret = {
    requestId: requestId,
    payload: {
      commands: [],
    },
  };
  for( var i = 0 ; i < body.inputs.length ; i++ ){
    if( body.inputs[i].intent == "action.devices.EXECUTE" ){
      for( var j = 0 ; j < body.inputs[i].payload.commands.length ; j++ ){
        var result = {
          ids:[],
          status: 'SUCCESS',
        };
        ret.payload.commands.push(result);
        var devices = body.inputs[i].payload.commands[j].devices;
        var execution = body.inputs[i].payload.commands[j].execution;
        for( var k = 0 ; k < execution.length ; k++ ){
          for( var l = 0 ; l < devices.length ; l++ ){
            var params = {
              intent: 'action.devices.EXECUTE',
              device_id: devices[l].id,
              command: execution[k].command,
              params: execution[k].params 
            };
            try{
              var message = await udpcomres.transceive(params, UDP_DEVICE_PORT, UDP_DEVICE_ADDRESS);
              result.ids.push(devices[l].id);
              result.states = message.states;

              await reportState(devices[l].id, message.states);
            }catch(error){
              console.error(error);
            }
          }
        }
      }
    }
  }

  console.log("onExecute result", JSON.stringify(ret));
  return ret;
});

app.onDisconnect((body, headers) => {
  console.info('onDisconnect');
  console.log('body', body);

//  var decoded = jwt_decode(headers.authorization);
//  console.log(decoded);

  // Return empty response
  return {};
});

exports.fulfillment = app;

async function reportState(id, states){
  var state = {
    requestId: String(++requestId),
    agentUserId: agentUserId,
    payload: {
      devices: {
        states:{
          [id]: states
        }
      }
    }
  };

  console.log("reportstate", JSON.stringify(state));
  await app.reportState(state);

  return state;
} 

const udpSocket = dgram.createSocket('udp4');

udpSocket.on('listening', () => {
  const address = udpSocket.address();
  console.log('UDP udpSocket listening on ' + address.address + ":" + address.port);
});
udpSocket.on('error', (err) => {
  console.error(`UDP Server error: ${err.message}`);
});

udpSocket.on('message', async (message, remote) => {
	var body = JSON.parse(message);
  console.log(JSON.stringify(body));
  var res = await reportState(body.payload.device_id, body.payload.states);
  console.log(res);
});

udpSocket.bind(UDP_REPORTSTATE_PORT);
