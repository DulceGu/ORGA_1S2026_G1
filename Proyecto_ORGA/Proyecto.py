import re
import serial
import time
import sys
import os

# Configuración del puerto serial
PUERTO = "COM4"
BAUDRATE = 9600
TIMEOUT_ESPERA = 5

# Comandos válidos del sistema
MODOS_VALIDOS = ["modo_fiesta", "modo_relajado", "modo_noche", "encender_todo", "apagar_todo"]
VALORES_VENTILADOR = ["ON", "OFF"]
VALORES_LEDS = ["Alternandose", "ON", "OFF", "Encendidos", "Apagados"]

def leer_archivo(ruta):
    """Lee el archivo .org y retorna sus líneas"""
    try:
        if not os.path.exists(ruta):
            print(f" Error: El archivo '{ruta}' no existe.")
            return None
        with open(ruta, 'r', encoding='utf-8') as archivo:
            return archivo.readlines()
    except Exception as e:
        print(f" Error al leer el archivo: {e}")
        return None

def validar_comando(linea, num_linea):
    """
    Valida una línea del archivo .org.
    Retorna (es_valido, mensaje_error)
    """
    linea_limpia = linea.strip()
    
    # Ignorar comentarios y líneas vacías
    if not linea_limpia or linea_limpia.startswith("//"):
        return True, None
    
    # Comandos de control de configuración
    if linea_limpia in ["conf_ini", "conf:fin"]:
        return True, None
    
    # Validar declaración de modo
    if linea_limpia.lower() in [m.lower() for m in MODOS_VALIDOS]:
        return True, None
    
    # Validar "Mensaje en LCD:" con comillas (máx 16 caracteres)
    if linea_limpia.startswith("Mensaje en LCD:"):
        patron = r'^Mensaje\s+en\s+LCD:\s*".{1,16}"$'
        if re.match(patron, linea_limpia, re.IGNORECASE):
            return True, None
        return False, f"Línea {num_linea}: Formato inválido para 'Mensaje en LCD'. Use: Mensaje en LCD: \"Texto\" (máx 16 caracteres)"
    
    # Validar "Ventilador:"
    if linea_limpia.startswith("Ventilador:"):
        try:
            valor = linea_limpia.split(":", 1)[1].strip().upper()
            if valor in VALORES_VENTILADOR:
                return True, None
            return False, f"Línea {num_linea}: Valor inválido para Ventilador. Use: ON u OFF"
        except IndexError:
            return False, f"Línea {num_linea}: Formato inválido para 'Ventilador:'"
    
    # Validar "LED'S:" o "LEDS:"
    if linea_limpia.startswith("LED'S:") or linea_limpia.startswith("LEDS:"):
        try:
            valor = linea_limpia.split(":", 1)[1].strip()
            valor_norm = valor.lower().replace(" ", "")
            valores_validos = [v.lower().replace(" ", "") for v in VALORES_LEDS]
            if valor_norm in valores_validos:
                return True, None
            return False, f"Línea {num_linea}: Valor inválido para LEDs. Use: {', '.join(VALORES_LEDS)}"
        except IndexError:
            return False, f"Línea {num_linea}: Formato inválido para 'LED'S:'"
    
    # Comando no reconocido
    return False, f"Línea {num_linea}: Comando no reconocido: '{linea_limpia}'"

def procesar_archivo(ruta):
    """Procesa y valida completamente el archivo .org"""
    lineas = leer_archivo(ruta)
    if lineas is None:
        return None

    errores = []
    configuracion_iniciada = False
    configuracion_finalizada = False
    modo_actual = None
    modos_encontrados = set()
    requerimientos = {"ventilador": False, "leds": False}

    for i, linea in enumerate(lineas, 1):
        linea_limpia = linea.strip()
        
        if not linea_limpia or linea_limpia.startswith("//"):
            continue
        
        es_valido, error_msg = validar_comando(linea, i)
        if not es_valido:
            errores.append(error_msg)
            continue
        
        # Manejo de conf_ini
        if linea_limpia == "conf_ini":
            if configuracion_iniciada:
                errores.append(f"Línea {i}: 'conf_ini' duplicado")
            else:
                configuracion_iniciada = True
            continue
        
        # Manejo de conf:fin
        if linea_limpia == "conf:fin":
            if not configuracion_iniciada:
                errores.append(f"Línea {i}: 'conf:fin' sin 'conf_ini' previo")
            elif modo_actual and not all(requerimientos.values()):
                faltan = [k for k, v in requerimientos.items() if not v]
                errores.append(f"Modo '{modo_actual}': Faltan parámetros: {', '.join(faltan)}")
            configuracion_finalizada = True
            modo_actual = None
            requerimientos = {"ventilador": False, "leds": False}
            continue
        
        # Validar que esté dentro de conf_ini/conf:fin
        if not configuracion_iniciada or configuracion_finalizada:
            errores.append(f"Línea {i}: Comando fuera del bloque de configuración")
            continue
        
        # Detectar nuevo modo
        if linea_limpia.lower() in [m.lower() for m in MODOS_VALIDOS]:
            if modo_actual and not all(requerimientos.values()):
                faltan = [k for k, v in requerimientos.items() if not v]
                errores.append(f"Modo '{modo_actual}': Faltan parámetros: {', '.join(faltan)}")
            
            modo_actual = linea_limpia.lower()
            if modo_actual in modos_encontrados:
                errores.append(f"Línea {i}: Modo '{modo_actual}' duplicado")
            modos_encontrados.add(modo_actual)
            requerimientos = {"ventilador": False, "leds": False}
            continue
        
        # Validar parámetros dentro de un modo
        if modo_actual:
            if linea_limpia.startswith("Ventilador:"):
                requerimientos["ventilador"] = True
            elif linea_limpia.startswith("LED'S:") or linea_limpia.startswith("LEDS:"):
                requerimientos["leds"] = True
    
    # Validaciones finales
    if not configuracion_iniciada:
        errores.append("Error: Falta la directiva 'conf_ini' al inicio del archivo")
    if not configuracion_finalizada:
        errores.append("Error: Falta la directiva 'conf:fin' al final del archivo")
    if not modos_encontrados:
        errores.append("Error: No se definió ningún modo válido")

    # Reportar resultados
    if errores:
        print("\n Errores de validación encontrados:")
        print("=" * 50)
        for error in errores:
            print(f"  • {error}")
        print("=" * 50)
        return None
    else:
        print(f" Archivo validado correctamente. Modos encontrados: {', '.join(modos_encontrados)}")
        return lineas

def esperar_ready(arduino, timeout=TIMEOUT_ESPERA):
    """Espera la señal READY del Arduino"""
    inicio = time.time()
    print(" Esperando señal READY del Arduino...")
    while time.time() - inicio < timeout:
        if arduino.in_waiting:
            linea = arduino.readline().decode(errors="ignore").strip()
            if linea and "READY" in linea.upper():
                print(" Arduino listo")
                return True
            elif linea:
                print(f" Arduino: {linea}")
        time.sleep(0.1)
    print(" Timeout: No se recibió READY del Arduino")
    return False

def enviar_a_arduino(lineas):
    """Envía las líneas validadas al Arduino vía serial"""
    try:
        with serial.Serial(PUERTO, BAUDRATE, timeout=1) as arduino:
            time.sleep(2)  # Esperar reset del Arduino
            
            if not esperar_ready(arduino):
                return False

            print(" Enviando configuración al Arduino...")
            for linea in lineas:
                linea_limpia = linea.strip()
                if not linea_limpia or linea_limpia.startswith("//"):
                    continue
                
                arduino.write((linea_limpia + "\n").encode('utf-8'))
                print(f"  → {linea_limpia}")
                
                time.sleep(0.15)  # Delay para procesamiento
                
                # Leer respuestas del Arduino
                while arduino.in_waiting:
                    respuesta = arduino.readline().decode(errors="ignore").strip()
                    if respuesta and "READY" not in respuesta.upper():
                        print(f"  ← {respuesta}")
            
            print(" Configuración enviada exitosamente")
            return True
            
    except serial.SerialException as e:
        print(f" Error de conexión serial: {e}")
        return False
    except Exception as e:
        print(f" Error inesperado: {e}")
        return False

def enviar_error_a_arduino():
    """Notifica al Arduino sobre un error de sintaxis"""
    try:
        with serial.Serial(PUERTO, BAUDRATE, timeout=2) as arduino:
            time.sleep(2)
            if esperar_ready(arduino, timeout=3):
                arduino.write(b"ERROR_SINTAXIS\n")
                print(" Comando de error enviado al Arduino")
                time.sleep(0.5)
                while arduino.in_waiting:
                    resp = arduino.readline().decode(errors="ignore").strip()
                    if resp:
                        print(f"  ← {resp}")
    except Exception as e:
        print(f"⚠️ No se pudo notificar error al Arduino: {e}")

def mostrar_ayuda():
    """Muestra información de uso del script"""
    print("\n Uso: python validador_org.py <archivo.org>")
    print("\nFormato válido del archivo .org:")
    print("""
    // Comentarios con //
    conf_ini
    modo_fiesta
    Mensaje en LCD: "Modo: FIESTA"
    Ventilador: ON
    LED'S: Alternandose
    conf:fin
    """)

def main():
    """Función principal"""
    if len(sys.argv) < 2 or sys.argv[1] in ["-h", "--help"]:
        mostrar_ayuda()
        return
    
    ruta_archivo = sys.argv[1]
    print(f" Validando archivo: {ruta_archivo}")
    
    lineas_validas = procesar_archivo(ruta_archivo)
    
    if lineas_validas:
        exito = enviar_a_arduino(lineas_validas)
        sys.exit(0 if exito else 1)
    else:
        print("\n No se enviará configuración debido a errores de validación")
        enviar_error_a_arduino()
        sys.exit(1)

if __name__ == "__main__":
    main()