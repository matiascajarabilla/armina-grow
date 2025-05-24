# Armina Grow
Implementacion en ESP32 para control de cultivo indoor.

## Intro
Firmware de ESP32 desarrollado con Gemini IA modelo 2.5 Pro (preview).

Hardware utilizado:
	- [Display LCD1602](https://protosupplies.com/product/lcd1602-16x2-i2c-blue-lcd-display/)
	- [Relay de 4 modulos](https://protosupplies.com/product/relay-module-5v-x-4-relay-w-opto-isolation/)
	- [Sensor DHT11](https://protosupplies.com/product/dht11-humidity-and-temp-sensor-module/) de humedad y temperatura
	- [Encoder rotativo](https://protosupplies.com/product/rotary-encoder-module/)

## Prompt
Vamos a hacer un prototipo IoT con una placa ESP32 NodeMCU, lo llamaremos Armina Grow. Yo me encargaré de configurar el hardware y vos de programar el software.

### Funcionamiento del dispositivo:

1. El sistema contará con un portal captivo para configurar la conexion a wifi, mostrará las redes disponibles y permitirá administrar las redes guardadas. El portal de configuracion será accesible desde un nombre o una ip que pueda predeterminarse

2. El prototipo contará con un sistema de archivos LittleFS para guardar configuraciones

3. Incluye los modulos:
	- Display LCD1602 16×2 I2C conectado a pines SDA=gpio21, SLC=gpio22
	- Relay de 4 modulos conectado a pines relay1=gpio18, relay2=gpio19, relay3=gpio23, relay4=gpio25
	- Sensor DHT11 de humedad y temperatura conectado a pin gpio=26
	- Encoder rotativo conectado a pines CLK=gpio13, DT=gpio14, SW=gpio27

4. El dispositivo debe tener fecha y hora actualizada

5. Modo Online: Si el dispositivo conecta con una red WiFi la pantalla de inicio mostrará en la primera linea la temperatura usando el simbolo T y la humedad usando el simbolo H; en la segunda linea mostrará 'Online' y la hora actual sin segundos. El LED integrado de la placa parpadeará 2 veces cada 3 segundos, indica funcionamiento Normal.

6. Modo Access Point: Si el dispositivo no se conecta a una red la pantalla mostrará en la primera linea 'Conectar a ' y en la segunda linea el nombre del access point para configurar la conexion del ESP32. El LED integrado de la placa parpadeará un segundo prendido y un segundo apagado, indica modo Conexión

7. Los relay y sensores serán programables de acuerdo a la configuración del menú

8. El encoder será usado para navegar un menú que se mostrará en el display LCD. Al mover el encoder se abrirá un menu de navegacion. 

9. El menu tendrá los siguientes items:
	- Temp. y humedad (asignado a DHT11)
		- Frecuencia: (cada cuántos minutos se toma muestra; valores admitidos enteros de 1 a 60)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el sensor tomará muestras cada 5 segundos; si es 'No' tomará muestras de acuerdo al valor de frecuencia guardado)
	- Luces (asignado a Relay 1)
		- Hora ON: (hora; valores admitidos enteros de 0 a 23)
		- Hora OFF: (hora; valores admitidos enteros de 0 a 23)
		- Atrás
	- Ventilacion (asignado a Relay 2)
		- Hora ON: (hora; valores admitidos enteros de 0 a 23)
		- Hora OFF: (hora; valores admitidos enteros de 0 a 23)
		- Atrás
	- Extraccion (asignado a Relay 3)
		- Frecuencia: (cada cuántas horas; valores admitidos enteros de 0 a 24; el 0 indica que el relay debe quedar apagado)
		- Duracion: (minutos; valores admitidos enteros de 1 a 60)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el relay se encenderá durante 1 segundo cada 5 segundos; si es 'No' se encenderá de acuerdo al valor de frecuencia guardado)
		- Atrás
	- Riego (asignado a Relay 4)
		- Frecuencia: (cada cuántos días; valores admitidos enteros de 0 a 30; el 0 indica que el relay debe quedar apagado)
		- Duracion: (minutos; valores admitidos enteros de 1 a 30)
		- Hora disparo: (hora; valores admitidos enteros de 0 a 23)
		- Modo test: (valores admitidos 'Si'/'No'; si es 'Si' el relay se encenderá durante 1 segundo cada 5 segundos; si es 'No' se encenderá de acuerdo al valor de frecuencia guardado)
		- Atrás
	- Sistema
		- Nombre red WiFi
		- Direccion IP (IP del Webserver si está en modo Online; IP de la pagina de configuracion wifi si esta en modo Access Point)
		- Olvidar Red WiFi (borra las credenciales wifi y resetea el dispositivo)
			- Confirmar
			- Atrás
		- Reiniciar
			- Confirmar
			- Atrás
		- Version de firmware
		- Atrás
		- Registro
			- Frecuencia: (cada cuántas horas, valores admitidos de 0 a 24; el 0 indica que no se guardan registros)
			- Borrar registro (borrar contenido del registro)
				- Confirmar
				- Atrás
	- Volver a inicio

10. Navegacion del menú: con clic se accede a cada item. Dentro del item con clic se accede a modificar el subitem, girando el encoder se ajusta el valor dentro de los admitidos y con clic se confirma el valor. Si no hay actividad con el encoder por 10 segundos la pantalla vuelve a la pantalla de inicio 

11. Cuando un relay cambie de estado o el sensor DHT tome una muestra la pantalla mostrará durante 2 segundos: en la primera linea el nombre la funcion asignada al relay; y en la segunda 'Encendido' o 'Apagado' según corresponda.

12. Los valores establecidos en el menú serán guardados en un archivo de preferencias dentro del sistema de archivos

13. completar LOG

* Webserver

### Comportamiento esperado
* La ejecucion de funciones y cambios de estados de ejecucion tendrán salida serial para monitorear el comportamiento y debuguear errores.
* El codigo estará comentado para mejorar la interpretacion.
* Antes de darme una respuesta preguntame todo lo que necesites, el codigo de resultado debe ser ....

## Disclaimer
Este codigo es experimental y de uso personal. Libre para copiar y modificar.