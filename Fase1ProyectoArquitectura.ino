#include <SPI.h>
#include <EEPROM.h>
#include <MFRC522.h>

const uint8_t PIN_RFID_SS   = 10;
const uint8_t PIN_595_LATCH = 6;

enum Estado : uint8_t {
  ESTADO_INICIO         = 0b00000001,
  ESTADO_ESPERANDO_RFID = 0b00000010,
  ESTADO_VALIDANDO_RFID = 0b00000100,
  ESTADO_ESPERANDO_PIN  = 0b00001000,
  ESTADO_VALIDANDO_PIN  = 0b00010000,
  ESTADO_AUTORIZADO     = 0b00100000,
  ESTADO_DENEGADO       = 0b01000000,
  ESTADO_ALARMA         = 0b10000000
};

const unsigned long TIEMPO_INICIO     = 2000;
const unsigned long TIEMPO_VALIDACION = 1000;
const unsigned long TIEMPO_RESULTADO  = 3000;

const uint8_t FIRMA_EEPROM = 0x5B;
const int     DIR_FIRMA    = 0;
const int     DIR_CANTIDAD = 1;
const int     DIR_USUARIOS = 2;
const uint8_t LARGO_UID    = 4;

struct Usuario {
  uint8_t  uid[LARGO_UID];
  uint16_t pin;
};

const Usuario USUARIOS_INICIALES[] = {
  { {0x35, 0x5A, 0x08, 0x01}, 1234 },
  { {0x61, 0x33, 0xB6, 0x06}, 5678 }
};
const uint8_t CANTIDAD_INICIAL = sizeof(USUARIOS_INICIALES) / sizeof(Usuario);

MFRC522 rfid(PIN_RFID_SS, UINT8_MAX);

Estado        estadoActual = ESTADO_INICIO;
unsigned long inicioEstado = 0;
uint8_t       uidLeido[LARGO_UID];
int           usuarioActual = -1;

void setup()
{
  Serial.begin(9600);
  SPI.begin();

  pinMode(PIN_595_LATCH, OUTPUT);
  digitalWrite(PIN_595_LATCH, LOW);

  cambiarEstado(ESTADO_INICIO);
  Serial.println(F("SISTEMA ACCESO"));
  Serial.println(F("INICIANDO..."));

  rfid.PCD_Init();
  delay(100);

  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("Version del lector: 0x"));
  Serial.println(version, HEX);
  if (version == 0x00 || version == 0xFF)
  {
    Serial.println(F("ERROR: no se detecta el lector RFID"));
  }

  inicializarEEPROM();
  mostrarUsuarios();
}

void loop()
{
  unsigned long transcurrido = millis() - inicioEstado;

  switch (estadoActual)
  {
    case ESTADO_INICIO:
      if (transcurrido >= TIEMPO_INICIO)
      {
        irAEsperaRFID();
      }
      break;

    case ESTADO_ESPERANDO_RFID:
      if (leerTarjeta())
      {
        cambiarEstado(ESTADO_VALIDANDO_RFID);
        Serial.print(F("Tarjeta detectada. UID: "));
        imprimirUID(uidLeido);
        Serial.println();
      }
      break;

    case ESTADO_VALIDANDO_RFID:
      if (transcurrido >= TIEMPO_VALIDACION)
      {
        validarTarjeta();
      }
      break;

    case ESTADO_ESPERANDO_PIN:
    case ESTADO_DENEGADO:
      if (transcurrido >= TIEMPO_RESULTADO)
      {
        irAEsperaRFID();
      }
      break;

    default:
      break;
  }
}

void cambiarEstado(Estado nuevo)
{
  estadoActual = nuevo;
  inicioEstado = millis();
  escribir595(nuevo);
}

void irAEsperaRFID()
{
  usuarioActual = -1;
  cambiarEstado(ESTADO_ESPERANDO_RFID);
  Serial.println();
  Serial.println(F("CONTROL ACCESO"));
  Serial.println(F("PRESENTE TARJETA"));
}

void validarTarjeta()
{
  usuarioActual = buscarUsuario(uidLeido);

  if (usuarioActual >= 0)
  {
    cambiarEstado(ESTADO_ESPERANDO_PIN);
    Serial.print(F("Tarjeta valida. Usuario "));
    Serial.println(usuarioActual + 1);
    Serial.println(F("INGRESE SU PIN"));
  }
  else
  {
    cambiarEstado(ESTADO_DENEGADO);
    Serial.println(F("TARJETA INVALIDA"));
    Serial.println(F("ACCESO DENEGADO"));
  }
}

void escribir595(uint8_t valor)
{
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPI.transfer(valor);
  SPI.endTransaction();

  digitalWrite(PIN_595_LATCH, HIGH);
  digitalWrite(PIN_595_LATCH, LOW);
}

bool leerTarjeta()
{
  if (!rfid.PICC_IsNewCardPresent())
  {
    return false;
  }
  if (!rfid.PICC_ReadCardSerial())
  {
    return false;
  }

  memset(uidLeido, 0, LARGO_UID);
  memcpy(uidLeido, rfid.uid.uidByte, min(rfid.uid.size, LARGO_UID));

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

void imprimirUID(const uint8_t *uid)
{
  for (uint8_t i = 0; i < LARGO_UID; i++)
  {
    if (uid[i] < 0x10)
    {
      Serial.print('0');
    }
    Serial.print(uid[i], HEX);
    Serial.print(' ');
  }
}

void inicializarEEPROM()
{
  if (EEPROM.read(DIR_FIRMA) == FIRMA_EEPROM)
  {
    Serial.println(F("Usuarios cargados desde la EEPROM."));
    return;
  }

  for (uint8_t i = 0; i < CANTIDAD_INICIAL; i++)
  {
    EEPROM.put(direccionUsuario(i), USUARIOS_INICIALES[i]);
  }
  EEPROM.update(DIR_CANTIDAD, CANTIDAD_INICIAL);
  EEPROM.update(DIR_FIRMA, FIRMA_EEPROM);
  Serial.println(F("Usuarios guardados en la EEPROM por primera vez."));
}

uint8_t cantidadUsuarios()
{
  return EEPROM.read(DIR_CANTIDAD);
}

int direccionUsuario(uint8_t indice)
{
  return DIR_USUARIOS + indice * sizeof(Usuario);
}

Usuario leerUsuario(uint8_t indice)
{
  Usuario usuario;
  EEPROM.get(direccionUsuario(indice), usuario);
  return usuario;
}

int buscarUsuario(const uint8_t *uid)
{
  for (uint8_t i = 0; i < cantidadUsuarios(); i++)
  {
    Usuario usuario = leerUsuario(i);
    if (memcmp(usuario.uid, uid, LARGO_UID) == 0)
    {
      return i;
    }
  }
  return -1;
}

void mostrarUsuarios()
{
  Serial.print(F("Usuarios registrados: "));
  Serial.println(cantidadUsuarios());

  for (uint8_t i = 0; i < cantidadUsuarios(); i++)
  {
    Usuario usuario = leerUsuario(i);
    Serial.print(F("  Usuario "));
    Serial.print(i + 1);
    Serial.print(F(" - UID: "));
    imprimirUID(usuario.uid);
    Serial.print(F("- PIN: "));
    Serial.println(usuario.pin);
  }
}