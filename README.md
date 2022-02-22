# Gopro-Max-Remote
Das ist die Entwicklung eines "Remote Controls" für die Gopro MAX, mit einen ESP32. 
Die Gopro öffnet einen Wifi hotspot, so dass man sich mit dem Telefon verbinden kann und entweder Bilder oder die Kamera bedienen kann. 
Man kann sie auch per Bluetooth "aufwecken", das habe ich probiert aber habe es nicht hingekriegt. 
## Versionen
Die verschiedene Versionen sind einerseits weil ich mich nicht die Mühe gemacht habe, seriös Versionierung zu machen (wie git..) und 
andererseits weil ich zuerst mit einen separaten 8266, dann ESP32 und separaten Display gearbeitet habe, am Ende habe ich es mit einen ESP32 wo ein OLED integriert hat [Ich glaube es war der da](https://www.bastelgarage.ch/esp32-new-wifi-kit-32-mit-32mbit-flash).
Muss man ein wenig reinschauen, aber die Prinzipien von Wifi und Display Benutzung sind überall etwa gleich.

Bei Version 8 habe ich statt Test im OLED, Bitmaps in einen HEX Array verwandelt, es gibt irgendwo eine Library oder online service der das macht...
