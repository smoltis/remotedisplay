#!/usr/bin/env python
import psutil
import paho.mqtt.client as paho
import time 
import datetime

# feed d3fum7ay9sl-incoming
# test.mosquitto.org 1883

BROKER="test.mosquitto.org"
PORT=1883
TOPIC="d3fum7ay9sl-incoming"

def publish(data):
    ret= client1.publish(TOPIC, data)                   #publish
    return

def get_cpu_ram():
    # gives a single float value
    cpu = psutil.cpu_percent()
    # ram
    ram = dict(psutil.virtual_memory()._asdict())
    currentDT = datetime.datetime.now()
    return "{} cpu {}% ram {}mb".format(currentDT.strftime("%H:%M"), cpu, ram['available']/(100 * 1024 * 1024))

#create client object
client1= paho.Client("d3fum7ay9sl-pc")                           
#establish connection
client1.connect(BROKER, PORT)  
client1.loop_start()

while True:
    data = get_cpu_ram()
    print(data)
    publish(data)
    time.sleep(10)

