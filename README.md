# Armina Grow
Implementacion en ESP32 para control de cultivo indoor.

Este es un ejercicio de programación del firmware de un ESP32 utilizando Gemini IA modelo 2.5 Pro (preview). Mis conocimientos de programación son escasos: algo de Python, algo de HTMLy CSS, estructuras básicas. Pretendo darle toda esta info a Gemini a ver si sale andando. Aprendiendo a usar git. Por ahora viendo que pasa

Hardware utilizado:
- [Display LCD1602](https://protosupplies.com/product/lcd1602-16x2-i2c-blue-lcd-display/)
- [Relay de 4 modulos](https://protosupplies.com/product/relay-module-5v-x-4-relay-w-opto-isolation/)
- [Sensor DHT11](https://protosupplies.com/product/dht11-humidity-and-temp-sensor-module/) de humedad y temperatura
- [Encoder rotativo](https://protosupplies.com/product/rotary-encoder-module/)

Info:
- [ESP32 Pinout randomnerdtutorials](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)
- [ESP32 Pinout electronicshub](https://www.electronicshub.org/esp32-pinout/)


## Disclaimer
Este codigo es experimental y de estudio. Libre para copiar y usar.

## Status de desarrollo
* Conexiones: OK
* Estructura de menu: OK 
* Funciones de menu: a verificar
* Navegacion: es necesario mejorar el funcionamiento del encoder
* Registro: OK
* Webserver:
	- Lectura sensores OK
	- Lectura de relays: ?
	- Botones de relays: ?
* Comportamiento segun preferencias guardadas: a verificar

Proximo: mejorar el funcionamiento del encoder

## Descripcion

El dispositivo se denomina Armina Grow y sirve para automatizar el cultivo indoor. Tiene un sensor de temperatura y humedad, permite controlar luces, ventilación, extracción y riego. La interface de usuario consta de un display LCD y un encoder rotativo que permite programar la operación del dispositivo. Cuenta con registro de datos y un servidor web para monitoreo. Me vas a dar el codigo que responda a las caracteristicas y funciones que siguen. Vas a esperar a que te diga Adelante para responder con el codigo.


### Modulos conectados:

- Display LCD1602 16×2 I2C
	SDA = gpio21,
	SLC = gpio22
- Relay de 4 modulos 
	relay1 = gpio18,
	relay2 = gpio19,
	relay3 = gpio23,
	relay4 = gpio25
- Sensor DHT11
 	data = gpio26
- Encoder rotativo
	CLK = gpio13,
	DT = gpio14,
	SW = gpio27


### Conexión

* Un portal captivo permite configurar la conexion a wifi. Muestra las redes disponibles y permite administrar las redes guardadas. El portal de configuracion es accesible desde un nombre o una ip predeterminada.

* Modo de operación Normal:
	- El dispositivo está conectado a una red WiFi.
	- Primera linea del LCD muestra la temperatura (simbolo T) y la humedad (simbolo H).
	- Segunda linea del LCD muestra 'Online' y la hora actual sin segundos.
	- El LED integrado de la placa parpadea 2 veces seguidas cada 3 segundos.

* Modo de operación Config:
	- El dispositivo no ha sido conectado a ninguna red previamente, el ESP32 entra en modo AP.
	- Primera linea del LCD muestra 'Conectar a '.
	- Segunda linea del LCD muestra el nombre del access point para configurar la conexión.
	- El LED integrado de la placa parpadea un segundo prendido y un segundo apagado.


### Navegación

* Se accede al menú girando el encoder en cualquier dirección.
* Los items del menu se recorren girando el encoder
* Se ingresa al item resaltado con el clic del encoder
* Se selecciona el valor girando el encoder
* Se confirma el valor con el clic del encoder
* Si no hay actividad con el encoder por 10 segundos se vuelve a la pantalla de inicio


### Estructura del menú

1. Temp. y humedad (asignado a DHT11)
	- Frecuencia: (
		cada cuántos minutos se toma muestra;
		valores admitidos enteros de 1 a 60;
		valor por default = 60)
	- Modo test (
		valores admitidos 'Si'/'No';
		'Si' = toma muestras cada 5 segundos;
		'No' = toma muestras de acuerdo al archivo de preferencias;
		valor por default = 'No')
2. Luces (asignado a Relay 1)
	- Hora ON: (
		hora;
		valores admitidos = enteros de 0 a 23;
		valor por default = 6)
	- Hora OFF: (
		hora;
		valores admitidos = enteros de 0 a 23;
		valor por default = 0)
	- Modo test: (
		valores admitidos 'Si'/'No';
		'Si' = se enciende durante 1 segundo cada 5 segundos;
		'No' = se enciende de acuerdo al archivo de preferencias;
		valor por default = 'No')
	- Atrás
3. Ventilacion (asignado a Relay 2)
	- Hora ON: (
		hora;
		valores admitidos = enteros de 0 a 23;
		valor por default = 6)
	- Hora OFF: (
		hora;
		valores admitidos = enteros de 0 a 23;
		valor por default = 0)
	- Modo test: (
		valores admitidos 'Si'/'No';
		'Si' = se enciende durante 1 segundo cada 5 segundos;
		'No' = se enciende de acuerdo al archivo de preferencias;
		valor por default = 'No')
	- Atrás
4. Extraccion (asignado a Relay 3)
	- Frecuencia: (
		cada cuántas horas;
		valores admitidos = enteros de 0 a 24;
		0 = el relay queda apagado;
		valor por default = 0)
	- Duracion: (
		minutos;
		valores admitidos = enteros de 1 a 60;
		valor por default = 1)
	- Modo test: (
		valores admitidos 'Si'/'No';
		'Si' = se enciende durante 1 segundo cada 5 segundos;
		'No' = se enciende de acuerdo al archivo de preferencias;
		valor por default = 'No')
	- Atrás
5. Riego (asignado a Relay 4)
	- Frecuencia: (
		cada cuántos días;
		valores admitidos = enteros de 0 a 30;
		0 = el relay queda apagado;
		valor por de fault = 0)
	- Duracion: (
		minutos;
		valores admitidos = enteros de 1 a 30;
		valor por default = 1)
	- Hora disparo: (
		hora; 
		valores admitidos = enteros de 0 a 23;
		valor por default = 21)
	- Modo test: (
		valores admitidos 'Si'/'No';
		'Si' = se enciende durante 1 segundo cada 5 segundos; 
		'No' = se enciende de acuerdo al archivo de preferencias;
		valor por default = 'No')
	- Atrás
6. Sistema
	- WiFi (mostrar nombre de red)
	- IP (mostrar 
		IP del Webserver si está en modo Online;
		IP de la pagina de configuracion wifi si esta en modo Config)
	- Olvidar Red WiFi (borrar las credenciales wifi y resetear el dispositivo)
		- Confirmar
		- Atrás
	- Reiniciar (resetear el dispositivo))
		- Confirmar
		- Atrás
	- Firmware (mostrar version)
	- Registro
		- Frecuencia: (
			cada cuántas horas,
			valores admitidos de 0 a 24;
			0 = indica que no se guardan registros;
			valor por default = 1)
		- Tamaño max: (
			cantidad máxima de registros;
			valores admitidos de 1000 a 9999;
			valor por default = 4380)
		- Borrar registro (borrar contenido del registro)
			- Confirmar
			- Atrás
		- Modo test: (
			valores admitidos 'Si'/'No';
			'Si' = frecuencia cada 10 segundos;
			'No' = frecuencia de acuerdo al archivo de preferencias;
			valor por default = 'No')
	- Atrás
7. Salir del menú


### Gestion de la información

* El dispositivo cuenta con un sistema de archivos LittleFS.

* Las preferencias de operacion configuradas en el menú se almacenan en el archivo armina-grow-pref.

* El registro datos de sensores y relays se almacena en armina-grow-log.csv.

* Datos de registro:
	- Fecha y dia en formato YYYY-MM-DD-HH-MM-SS
	- Temperatura
	- Humedad
	- Estado de relay Luces: ON/OFF
	- Estado de relay Ventilación: ON/OFF
	- Estado de relay Extracción: ON/OFF
	- Estado de relay Riego: ON/OFF

* El sistema cuenta con un webserver de monitoreo

* La pagina de monitoreo en el servidor igual a armina-grow-monitor_refencia.html (el archivo html se adjunta aparte). Deberás adaptarlo para que funciona en este contexto.


### Condiciones de ejecución

* La fecha y hora se actualiza con Network Time Protocol

* La operacion de relays y sensores está determinada por los parametros guardados en el archivo de preferencias.

* Si un relay cambia de estado:
	- la pantalla LCD mostrará durante 2 segundos
	- linea 1 = nombre la funcion asignada al relay
	- linea 2 = 'Encendido' o 'Apagado' según corresponda

* El registro se actualiza con la frecuencia establecida en armina-grow-pref.

* Si la cantidad de registros supera la cantidad máxima de registros establecida en el archivo de preferencias se borra el registro más antiguo antes de grabar el siguiente.

* Si se pierde la conexion a internet la segunda linea del LCD muestra 'Offline'

* La ejecucion de funciones y cambios de estados de ejecucion tendrán una salida serial descriptiva para monitorear el comportamiento desde consola.


## Post prompt

* El codigo estará comentado para mejorar la interpretacion

* Considerar las limitaciones de memoria del hardware

* Me vas a preguntar lo que necesites saber

* Me vas a proponer mejoras
