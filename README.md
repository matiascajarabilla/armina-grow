# Armina Grow
Implementacion en ESP32 para control de cultivo indoor.

## Intro
Firmware de ESP32 desarrollado con Gemini IA modelo 2.5 Pro (preview).

Hardware utilizado:
- [Display LCD1602](https://protosupplies.com/product/lcd1602-16x2-i2c-blue-lcd-display/)
- [Relay de 4 modulos](https://protosupplies.com/product/relay-module-5v-x-4-relay-w-opto-isolation/)
- [Sensor DHT11](https://protosupplies.com/product/dht11-humidity-and-temp-sensor-module/) de humedad y temperatura
- [Encoder rotativo](https://protosupplies.com/product/rotary-encoder-module/)

Info:
- [ESP32 Pinout randomnerdtutorials](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)
- [ESP32 Pinout electronicshub](https://www.electronicshub.org/esp32-pinout/)

## Prompt

### Instrucciones

Vas a programar un prototipo IoT con una placa ESP32 NodeMCU en Arduino IDE. El dispositivo se denomina Armina Grow y sirve para automatizar el cultivo indoor. Tiene un sensor de temperatura y humedad, permite controlar luces, ventilación, extracción y riego. La interface de usuario consta de un display LCD y un encoder rotativo que permite programar la operación del dispositivo. Cuenta con registro de datos y un servidor web para monitoreo. Me vas a dar el codigo que responda a las caracteristicas y funciones que siguen:

### Modulos conectados:

- Display LCD1602 16×2 I2C conectado a pines SDA=gpio21, SLC=gpio22
- Relay de 4 modulos conectado a pines relay1=gpio18, relay2=gpio19, relay3=gpio23, relay4=gpio25
- Sensor DHT11 de humedad y temperatura conectado a pin gpio=26
- Encoder rotativo conectado a pines CLK=gpio13, DT=gpio14, SW=gpio27

### Conexión

* Tiene un portal captivo de configurar de la conexion a wifi. Muestra las redes disponibles y permite administrar las redes guardadas. El portal de configuracion es accesible desde un nombre o una ip predeterminada.

* Modos de operacion:

	- Modo Online:
		- El dispositivo se conecta con una red WiFi.
		- Primera linea del LCD muestra la temperatura (simbolo T) y la humedad (simbolo H).
		- Segunda linea del LCD muestra 'Online' y la hora actual sin segundos.
		- El LED integrado de la placa parpadea 2 veces seguidas cada 3 segundos.

	- Modo Config:
		- El dispositivo no se conecta a una red WiFi, el ESP32 entra en modo AP.
		- Primera linea del LCD muestra 'Conectar a '.
		- Segunda linea del LCD muestra el nombre del access point para configurar la conexión.
		- El LED integrado de la placa parpadea un segundo prendido y un segundo apagado.

### Navegación

* Se accede al menú girando el encoder en cualquier dirección. Con clic se ingresa a la opciones de cada item

* El encoder será usado para navegar un menú que se mostrará en el display LCD. Al mover el encoder en cualquier sentido se abrirá el menu.


* El menu tendrá los siguientes items:
	1. Temp. y humedad (asignado a DHT11)
		- Frecuencia: (cada cuántos minutos se toma muestra; valores admitidos enteros de 1 a 60; valor por default=60)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el sensor tomará muestras cada 5 segundos; si es 'No' tomará muestras de acuerdo al valor de frecuencia guardado; valor por default='No')
	2. Luces (asignado a Relay 1)
		- Hora ON: (hora; valores admitidos enteros de 0 a 23; valor por default=6)
		- Hora OFF: (hora; valores admitidos enteros de 0 a 23; valor por default=0)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el relay se encenderá durante 1 segundo cada 5 segundos; si es 'No' se encenderá de acuerdo a los valores definidos en Hora ON / Hora OFF; valor por default='No')
		- Atrás
	3. Ventilacion (asignado a Relay 2)
		- Hora ON: (hora; valores admitidos enteros de 0 a 23; valor por default=6)
		- Hora OFF: (hora; valores admitidos enteros de 0 a 23; valor por default=0)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el relay se encenderá durante 1 segundo cada 5 segundos; si es 'No' se encenderá de acuerdo a los valores definidos en Hora ON / Hora OFF; valor por default='No')
		- Atrás
	4. Extraccion (asignado a Relay 3)
		- Frecuencia: (cada cuántas horas; valores admitidos enteros de 0 a 24; el 0 indica que el relay debe quedar apagado)
		- Duracion: (minutos; valores admitidos enteros de 1 a 60)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el relay se encenderá durante 1 segundo cada 5 segundos; si es 'No' se encenderá de acuerdo al valor de frecuencia guardado; valor por default='No')
		- Atrás
	5. Riego (asignado a Relay 4)
		- Frecuencia: (cada cuántos días; valores admitidos enteros de 0 a 30; el 0 indica que el relay debe quedar apagado: valor por default=0)
		- Duracion: (minutos; valores admitidos enteros de 1 a 30)
		- Hora disparo: (hora; valores admitidos enteros de 0 a 23)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el relay se encenderá durante 1 segundo cada 5 segundos; si es 'No' se encenderá de acuerdo al valor de frecuencia guardado; valor por default='No')
		- Atrás
	6. Sistema
		- Nombre red WiFi (mostrar nombre de red)
		- Direccion IP (mostrar IP del Webserver si está en modo Online; IP de la pagina de configuracion wifi si esta en modo Config)
		- Olvidar Red WiFi (borrar las credenciales wifi y resetear el dispositivo)
			- Confirmar
			- Atrás
		- Reiniciar (resetear el dispositivo))
			- Confirmar
			- Atrás
		- Version de firmware (mostrar version)
		- Atrás
		- Registro
			- Frecuencia: (cada cuántas horas, valores admitidos de 0 a 24; el 0 indica que no se guardan registros)
			- Tamaño max: (cantidad máxima de registros del archivo csv; valor por default=4380)
			- Borrar registro (borrar contenido del registro)
				- Confirmar
				- Atrás
	7. Volver a inicio

* Navegacion del menú: con clic se accede a cada item. Dentro del item con clic se accede a modificar el subitem, girando el encoder se ajusta el valor dentro de los admitidos y con clic se confirma el valor. Si no hay actividad con el encoder por 10 segundos la pantalla vuelve a la pantalla de inicio 
* El dispositivo debe tener fecha y hora actualizada

* Cuando un relay cambie de estado o el sensor DHT tome una muestra la pantalla LCD mostrará durante 2 segundos: en la primera linea el nombre la funcion asignada al relay; y en la segunda 'Encendido' o 'Apagado' según corresponda.

### Gestion de información

* Cuenta con un sistema de archivos LittleFS para almacenar datos.

* La operacion de relays y sensores está determinada por los parametros de configuración del menú. Los valores establecidos en el menú serán guardados en un archivo de nombre armina-grow-pref dentro del sistema de archivos

* El dispositivo guardará un archivo de registro llamado armina-grow-log.csv. Se actualizará con la frecuencia establecida en en el menu Sistema/Registro/Frecuencia:
	- Fecha y dia en formato YYYY-MM-DD-HH-MM-SS
	- Temperatura
	- Humedad
	- Estado de relay Luces: ON/OFF
	- Estado de relay Ventilación: ON/OFF
	- Estado de relay Extracción: ON/OFF
	- Estado de relay Riego: ON/OFF

* Si el archivo csv alcanza la cantidad máxima de registros establecida en Sistema/Registro/Tamaño max se borrará el registro más antiguo antes de grabar el siguiente.

* Webserver

### Respuesta esperada

* La ejecucion de funciones y cambios de estados de ejecucion tendrán salida serial para monitorear el comportamiento y debuguear errores.

* El codigo estará comentado para mejorar la interpretacion.

* Antes de darme una respuesta preguntame todo lo que necesites, el codigo de resultado debe ser ....

## Disclaimer
Este codigo es experimental y de uso personal. Libre para copiar y modificar.