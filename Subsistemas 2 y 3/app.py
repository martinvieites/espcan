from flask import Flask, render_template, request, redirect
import paho.mqtt.client as mqtt
import esp32can
import cantools
import threading
import time
import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS
import csv

# Servidor web
app = Flask(__name__)
mqttc = mqtt.Client("server")

# DBC
db = cantools.database.load_file('dbc/obd2.dbc')
message_decoder = db.get_message_by_name('OBD2')

# Influxdb
INFLUXDB_TOKEN="KCP0qPvVOypcJkNPLTV_9moRaqRCU-VZueqytjmiOKbA90uHsGZNRXZoUU0EBCfxLOvlO73ywQJXjWBHSufIDQ=="
org = "home"
url = "http://influxdb2:8086"
bucket="obd2"

influxdb_client_c = influxdb_client.InfluxDBClient(
   url=url,
   token=INFLUXDB_TOKEN,
   org=org
)
write_api = influxdb_client_c.write_api(write_options=SYNCHRONOUS)

# DTCs
reader = csv.reader(open('./dtc/obd-trouble-codes.csv', 'r'))
dtc_dict = {}
for row in reader:
   k, v = row
   dtc_dict[k] = v

# Dispositivos registrados
dongle_list = []

# Listado de dispositivos
@app.get("/")
def get_dongle_list():
    return render_template('main.html', devices = dongle_list)

# Listado de códigos de error
@app.get("/<dongle_id>/dtc")
def dongle_dtc(dongle_id):
    dongle_found = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]

    if len(dongle_found) == 0:
        return ('', 404)
    else:
        # Solicitar DTCs
        mqttc.publish(f"dongle/{dongle_found[0].identifier}/action/dtc", 1)

        time.sleep(5)

        # Mostrar códigos de error
        return render_template('dtc.html', dongle = dongle_found[0])

    return render_template('base.html')

# Listado de identificadores
@app.get("/<dongle_id>/pids")
def get_dongle_pids(dongle_id):
    dongle_found = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]

    if len(dongle_found) == 0:
        return ('', 404)
    else:
        # Comprobar si existen identificadores
        if dongle_found[0].have_pids():
            return render_template('pids.html', pids_list = dongle_found[0].pids)
        else:
            mqttc.publish(f"dongle/{dongle_found[0].identifier}/action/pids", 1)
            while True:
                if dongle_found[0].have_pids():
                    break
                else:
                    # Esperar a que existan identificadores disponibles
                    time.sleep(5)
            return render_template('pids.html', pids_list = dongle_found[0].pids)

# Control de la monitorización
@app.get("/<dongle_id>/monitor")
def dongle_monitor(dongle_id):

    dongle_found = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
    if len(dongle_found) == 0:
        return ('', 404)
    else:
        return render_template('monitor.html', pids_list = dongle_found[0].pids, dongle = dongle_found[0])

# Iniciar monitorización
@app.post("/<dongle_id>/monitor")
def dongle_monitor_start(dongle_id):
    dongle_found = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
    if len(dongle_found) == 0:
        return ('', 404)
    else:
        request_bytes = b''
        for el in request.form:
            res = next(( pid for pid in dongle_found[0].pids if pid["id"] == el))
            request_bytes += res["address"]
        dongle_found[0].monitoring = 1
        mqttc.publish(f"dongle/{dongle_found[0].identifier}/action/monitor", request_bytes)
        return redirect('/')

# Finalizar monitorización
@app.post("/<dongle_id>/monitor/stop")
def dongle_monitor_stop(dongle_id):
    dongle_found = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
    if len(dongle_found) == 0:
        return ('', 404)
    else:
        dongle_found[0].monitoring = 0
        mqttc.publish(f"dongle/{dongle_found[0].identifier}/action/monitor", 0)
        return redirect('/')

# Hilo del servidor web
def flask_thread():
    app.run(host="0.0.0.0", port=5000, debug=False)

# Callback de los mensajes MQTT
def on_message_mqtt(client, userdata, message):

    data = message.topic.split('/')
    dongle_id = data[1]
    action = data[2]

    match action:
        # Actualizar el estado del dispositivo
        case 'status':
            # Comprobar si el dispositivo está registrado
            # De no estarlo proceder con el registro
            dongle_temp = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
            if len(dongle_temp) == 0:
                # Registro del dispositivo
                dongle_list.append(esp32can.Esp32Can(dongle_id, int(message.payload)))
                # Solicitud del identificador VIN asociado
                mqttc.publish(f"dongle/{dongle_id}/action/vin", 1)
            else:
                # Actualizar estado
                dongle_temp[0].set_status(message.payload)

        # Recibir listado de identificadores
        case 'pids':
            dongle_temp = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
            if len(dongle_temp) != 0:
                tmp_pids = []
                choices = message_decoder.signals[3].choices
                dongle_temp = [dongle for dongle in dongle_list if dongle.identifier == dongle_id][0]
                decoded_message = message_decoder.decode(message.payload)

                s01pid = str(decoded_message["S01PID"])
                pids_avaliable = bin(decoded_message[s01pid])[2:]
                offset = int(s01pid[-5:-3])

                for idx,el in enumerate(pids_avaliable):
                    if el == "1":
                        tmp_split = str(choices.get(idx+offset))
                        if len(tmp_split) > 1:
                            tmp = str(choices.get(idx+offset)).split('_')
                            if tmp[0] != "None":
                                tmp_pids.append({
                                    "id": tmp[0],
                                    "info": tmp[-1],
                                    "address": bytes.fromhex(tmp[0][-2:])
                                })

                dongle_temp.add_pids(tmp_pids)

        # Recibir identificador VIN del vehículo
        case 'vin':
            dongle_temp = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
            if len(dongle_temp) != 0:
                dongle_temp[0].set_vin(message.payload)

        # Recibir datos de monitorización
        case 'data':
            dongle_temp = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]
            if len(dongle_temp) != 0:
                s01pid = str(message_decoder.decode(message.payload)["S01PID"])
                measurement = s01pid.split('_')[-1]
                value = message_decoder.decode(message.payload)[s01pid]
                p = influxdb_client.Point(measurement).tag("device", dongle_temp[0].identifier).field("value", value)
                write_api.write(bucket=bucket, org=org, record=p)
                dongle_temp[0].write_data(message_decoder.decode(message.payload))

        # Recibir códigos de error
        case 'dtc':
            dongle_temp = [dongle for dongle in dongle_list if dongle.identifier == dongle_id]

            if len(dongle_temp) != 0:
                # Borrar los códigos almacenados al recibir otros nuevos
                dongle_temp[0].reset_dtc()
                num_dtc = int(data[-1])

                dongle_temp[0].set_dtc(num_dtc)
                if num_dtc != 0:
                    # Almacenar la información y los códigos
                    print(message.payload)
                    for i in range(0, 2*num_dtc, 2):
                        output = str(message.payload[i:i+2])
                        code = output[2:-1].replace("\\x", '')

                        dongle_temp[0].add_dtc({
                            "code": "P"+code,
                            "info": dtc_dict["P"+code]
                        })


# Hilo encargado de la comunicación
def mqtt_thread():
    mqttc.on_message = on_message_mqtt

    mqttc.username_pw_set("mqttuser", "mqttpasswd")
    mqttc.connect("mosquitto")
    mqttc.subscribe("dongle/+/status")
    mqttc.subscribe("dongle/+/pids")
    mqttc.subscribe("dongle/+/data")
    mqttc.subscribe("dongle/+/vin")
    mqttc.subscribe("dongle/+/vin")
    mqttc.subscribe("dongle/+/dtc/#")
    mqttc.loop_forever()

# Hilo principal
def main():
    t1 = threading.Thread(target=flask_thread, args=())
    t2 = threading.Thread(target=mqtt_thread, args=())
    t1.start()
    t2.start()
    t1.join()
    t2.join()

if __name__ == "__main__":
    main()
