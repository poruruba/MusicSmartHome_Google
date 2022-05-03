/// <reference types="@google/local-home-sdk" />

import App = smarthome.App;
import Constants = smarthome.Constants;
import DataFlow = smarthome.DataFlow;
import Execute = smarthome.Execute;
import Intents = smarthome.Intents;
import IntentFlow = smarthome.IntentFlow;

const UDP_DEVICE_PORT = 3311;

class LocalExecutionApp
{
  constructor(private readonly app: App)
  {
  }

  identifyHandler(request: IntentFlow.IdentifyRequest): Promise<IntentFlow.IdentifyResponse>
  {
    console.log("IDENTIFY intent: " + JSON.stringify(request, null, 2));

    const scanData = request.inputs[0].payload.device.udpScanData;
    if (!scanData) {
      const err = new IntentFlow.HandlerError(request.requestId, 'invalid_request', 'Invalid scan data');
      return Promise.reject(err);
    }

    const payload = Buffer.from(scanData.data, 'hex').toString();
    const resultDevice = JSON.parse(payload);

    const response: IntentFlow.IdentifyResponse = {
      intent: Intents.IDENTIFY,
      requestId: request.requestId,
      payload: {
        device: {
          id: resultDevice.device_id,
          verificationId: resultDevice.local_device_id,
        }
      }
    };
    console.log("IDENTIFY response: " + JSON.stringify(response, null, 2));

    return Promise.resolve(response);
  }

  queryHandler(request: IntentFlow.QueryRequest): Promise<IntentFlow.QueryResponse>
  {
    console.log("QUERY intent: " + JSON.stringify(request, null, 2));

    const response: IntentFlow.QueryResponse = {
      requestId: request.requestId,
      payload: {
        devices: {
        }
      }
    };
    const promises: Array<Promise<void>> = request.inputs[0].payload.devices.map((device) => {
      console.log("Handling QUERY intent for device: " + JSON.stringify(device));
  
      var data = {
        msgId: 0,
        payload: {
          intent: request.inputs[0].intent,
          device_id: device.id,
        }
      };
      const radioCommand = new DataFlow.UdpRequestData();
      radioCommand.requestId = request.requestId;
      radioCommand.deviceId = device.id;
      radioCommand.port = UDP_DEVICE_PORT;
      radioCommand.data = Buffer.from(JSON.stringify(data)).toString('hex');
      radioCommand.expectedResponsePackets = 1;

      console.log("Sending UDP request to the smart home device:", JSON.stringify(data));

      return this.app.getDeviceManager()
        .send(radioCommand)
        .then((result: DataFlow.CommandSuccess) => {
          console.log(JSON.stringify(result));
          var value = result as DataFlow.UdpResponseData;
          var message = Buffer.from(value.udpResponse.responsePackets![0], 'hex').toString();
          console.log('message:' + message);
          var resultDevice = JSON.parse(message).payload;

          Object.assign(response.payload.devices, { [resultDevice.device_id] : resultDevice.states });
          console.log(`Command successfully sent to ${device.id}`);
        })
        .catch((e: IntentFlow.HandlerError) => {
          e.errorCode = e.errorCode || 'invalid_request';
          console.error('An error occurred sending the command', e.errorCode);
        });
    });
    
    return Promise.all(promises)
      .then(() => {
        console.log("QUERY response: " + JSON.stringify(response, null, 2));
        return response;
      })
      .catch((e) => {
        const err = new IntentFlow.HandlerError(request.requestId, 'invalid_request', e.message);
        return Promise.reject(err);
      });    
  }
    
  executeHandler(request: IntentFlow.ExecuteRequest): Promise<IntentFlow.ExecuteResponse>
  {
    console.log("EXECUTE intent: " + JSON.stringify(request, null, 2));

    const command = request.inputs[0].payload.commands[0];
    const execution = command.execution[0];
    const response = new Execute.Response.Builder()
      .setRequestId(request.requestId);

    const promises: Array<Promise<void>> = command.devices.map((device) => {
      console.log("Handling EXECUTE intent for device: " + JSON.stringify(device));

      var data = {
        msgId: 0,
        payload: {
          intent: request.inputs[0].intent,
          device_id: device.id,
          command: execution.command,
          params: execution.params
        }
      };
      const radioCommand = new DataFlow.UdpRequestData();
      radioCommand.requestId = request.requestId;
      radioCommand.deviceId = device.id;
      radioCommand.port = UDP_DEVICE_PORT;
      radioCommand.data = Buffer.from(JSON.stringify(data)).toString('hex');
      radioCommand.expectedResponsePackets = 1;

      console.log("Sending UDP request to the smart home device:", JSON.stringify(data));

      return this.app.getDeviceManager()
        .send(radioCommand)
        .then((result: DataFlow.CommandSuccess) => {
          console.log(JSON.stringify(result));
          var value = result as DataFlow.UdpResponseData;
          var message = Buffer.from(value.udpResponse.responsePackets![0], 'hex').toString();
          console.log('message:' + message);
          var resultDevice = JSON.parse(message).payload;

          response.setSuccessState(resultDevice.device_id, resultDevice.states);
          console.log(`Command successfully sent to ${device.id}`);
        })
        .catch((e: IntentFlow.HandlerError) => {
          e.errorCode = e.errorCode || 'invalid_request';
          response.setErrorState(device.id, e.errorCode);
          console.error('An error occurred sending the command', e.errorCode);
        });
     });

    return Promise.all(promises)
      .then(() => {
        var result = response.build();
        console.log("EXECUTE response: " + JSON.stringify(result, null, 2));
        return result;
      })
      .catch((e) => {
        const err = new IntentFlow.HandlerError(request.requestId, 'invalid_request', e.message);
        return Promise.reject(err);
      });
  }
}

const localHomeSdk = new App('1.0.0');
const localApp = new LocalExecutionApp(localHomeSdk);
localHomeSdk
  .onIdentify(localApp.identifyHandler.bind(localApp))
  .onQuery(localApp.queryHandler.bind(localApp))
  .onExecute(localApp.executeHandler.bind(localApp))
  .listen()
  .then(() => console.log('Ready'))
  .catch((e: Error) => console.error(e));
