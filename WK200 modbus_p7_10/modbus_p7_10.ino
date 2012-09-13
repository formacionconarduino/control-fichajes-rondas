//WK0200 -> ATMEGA 328P -> Graba datos tarjeta en EEPROM (1k - 0 a 0x3ff)

#include <SimpleModbusSlave.h>
#include <Wire.h>
#include <EEPROM.h>

#define ADDR_RFID 0x41                  //Direccion I2C del lector RFID en el WK0200

#define BAUD 19200
#define DirMB 60

#define EP_punteroTarjetas 20
#define EP_puertabloqueada 19

enum
{ COMANDO, ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7, ARG8, ARG9, ARG10, TOTAL_ERRORS, TOTAL_REGS_SIZE };
   
unsigned int holdingRegs[TOTAL_REGS_SIZE];    

boolean rfid=false, abrir=false, puerta_abierta=false, controlar=false;
unsigned long numero, numero_tarjeta=0, time, time2;



void setup()
{
  Wire.begin();        //Inicia I2C
  rfidReleOff();       //Desconectar rele        
     
  for (int i=0; i<TOTAL_REGS_SIZE; i++) { holdingRegs[i]=0; }
 
  modbus_configure (BAUD, DirMB,0,TOTAL_REGS_SIZE);
  
  if (EEPROM.read(EP_puertabloqueada)==0xFF)
  {
    EEPROM.write(EP_puertabloqueada, 0x00);  //inicializar puerta desbloqueada
  }
}

void loop()
{
  if (puerta_abierta&&(millis()>(time+2000)))
  {
    rfidReleOff();
    puerta_abierta=false;
  }
  
  holdingRegs[COMANDO]=0xFFFF;
  holdingRegs[TOTAL_ERRORS]=modbus_update(holdingRegs);
  if(holdingRegs[COMANDO]!=0xFFFF)
  {
    ProcesarComandoMB();
  }
  
  CompruebaRFID();
  if (rfid==true)
  { 
    boolean encontrada=false;
    unsigned char info;
    unsigned int punterotarjetas=EP_punteroTarjetas;
    unsigned long n;
    
    rfid=abrir=false;
    
    if ((controlar)&&(millis()<(time2+5000)))
    {
      anadirtarjetaEEPROM(numero_tarjeta,0);
      controlar=false;
    }
    else
    {
      controlar=false;
      while((EEPROM.read(punterotarjetas+4)!=0xFF)&&(!encontrada))
      {
        n=leer_numero_tarjeta(punterotarjetas);
        info=EEPROM.read(punterotarjetas+4);

        if (n==numero_tarjeta)
        {
          encontrada=true;
          switch(info)
          {
            case 0x21:  //es supermaestra y habilitada
                        abrir=true; controlar=true; time2=millis();
                        break;
            case 0x11:  //es maestra y habilitada 
            case 0x01:  //es normal y habilitada   
                        if (!puerta_bloqueada()) { abrir=true; }
                        break;
          }  
        }      
        punterotarjetas=punterotarjetas+5;
      }  

      if (puerta_bloqueada())
      {
        holdingRegs[ARG8]=0x0100;      //puerta bloqueada
      }
      else
      {
        holdingRegs[ARG8]=0x0000;      //puerta desbloqueada   
      }   
      if (encontrada)
      {
        holdingRegs[ARG8]=holdingRegs[ARG8]|info;    //tarjeta encontrada -> byte MSB: puerta bloqueada o no   
      }                                              //                   -> byte LSB: tipo tarjeta+habilitada o no      
      else 
      { 
        holdingRegs[ARG8]=holdingRegs[ARG8]|0xFF;    //tarjeta no encontrada -> byte MSB: puerta bloqueada o no
      }                                              //                      -> byte LSB: 0xFF (no dada de alta)                                                          
    }
    
    if (abrir)
    {
      rfidReleOn();
      time = millis();
      puerta_abierta=true;
    }
  }
}

void ProcesarComandoMB(void)
{ 
  unsigned int punterotarjetas=EP_punteroTarjetas;
  numero=(((unsigned long)holdingRegs[ARG1])<<16)|((unsigned long)holdingRegs[ARG2]);
  switch(holdingRegs[COMANDO])
  {
    case 0x01:  //Nueva Direccion ModBus
                break;
    case 0x02:  //Nuevos Parametros COM del ModBus
                break;
    case 0x03:  //Validar los nuevos parametros ModBus
                break;
    case 0x20:  //Abrir puerta de forma remota
                rfidReleOn();
                time = millis();
                puerta_abierta=true;
                break;  
    case 0x21:  //Dar de alta tarjeta normal
                anadirtarjetaEEPROM(numero,0);
                break;   
    case 0x22:  //Dar de alta tarjeta maestra
                anadirtarjetaEEPROM(numero,1);
                break;
    case 0x23:  //Dar de alta tarjeta supermaestra
                anadirtarjetaEEPROM(numero,2);
                break;   
    case 0x24:  //Dar de baja tarjeta (da igual el tipo)
                dardebajatarjetaEEPROM(numero);
                break;    
    case 0x25:  //Bloquear puerta
                EEPROM.write(EP_puertabloqueada,0x01);
                break;                
    case 0x26:  //Desbloquear puerta
                EEPROM.write(EP_puertabloqueada,0x00);
                break;     
    case 0x27:  //Consultar puertas
                break;                
    case 0x28:  //Borrado total cerradura             
                for (int i=0; i<TOTAL_REGS_SIZE; i++)
                {
                  holdingRegs[i]=0;
                }
                while (EEPROM.read(punterotarjetas+4)!=0xFF)
                {
                  for (int i=0; i<5; i++)
                  {
                    EEPROM.write(punterotarjetas,0xFF);
                    punterotarjetas++;
                  }
                }
                break;                  
    case 0x2c:  //Borrar registros RTU con informacion ya leida -> continuacion comando TCP
                holdingRegs[ARG6]=0;
                holdingRegs[ARG7]=0;  
                holdingRegs[ARG8]=0;  
                break;          
  }   
}

void CompruebaRFID (void)
{
  int c=0;
  byte codigo[5]={0,0,0,0,0};

  Wire.requestFrom(ADDR_RFID, 1);
  if (Wire.available())
  {
    c=Wire.read();

    if (c==0x6E)      //Tarjeta detectada
    {
      rfid=true;
         
      Wire.beginTransmission(ADDR_RFID);
      Wire.write(0x52);
      Wire.endTransmission();
      Wire.requestFrom(ADDR_RFID,5);
    
      for (int i=0; i<5; i++)
      {
        if (Wire.available())
        {
          codigo[i]=Wire.read();        //El primer byte ¿es el de mayor peso?
        }
      }
    
      if (codigo[4]==0x4E)  //seguna parte de la transmision del codigo
      { 
        Wire.beginTransmission(ADDR_RFID);
        Wire.write(0x52);
        Wire.endTransmission();
        Wire.requestFrom(ADDR_RFID,1);
        if (Wire.available())
        {
          codigo[4]=Wire.read();
        }
      }
      numero_tarjeta= (((unsigned long)codigo[1])<<24)|(((unsigned long)codigo[2])<<16)|(((unsigned long)codigo[3])<<8)|((unsigned long)codigo[4]);   
      holdingRegs[ARG6]=(codigo[1]<<8)|codigo[2];
      holdingRegs[ARG7]=(codigo[3]<<8)|codigo[4];
    } 
  }
}

void rfidReleOn()
{
  Wire.beginTransmission(ADDR_RFID);
  Wire.write(0x62);
  Wire.write(0x01);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write(0x63);
  Wire.endTransmission();
}

void rfidReleOff()
{
  Wire.beginTransmission(ADDR_RFID);
  Wire.write(0x62);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write((byte)0x00);
  Wire.write(0x62);
  Wire.endTransmission();
}

boolean puerta_bloqueada (void)
{
  if (EEPROM.read(EP_puertabloqueada)==0x01) return true;
  else return false;
}

void anadirtarjetaEEPROM(unsigned long n_leer, int tipo)
{
  unsigned char info;
  int escrito=0, resto, punterotarjetas=EP_punteroTarjetas;
  unsigned long n;
  
  do
  {
    if (EEPROM.read(punterotarjetas+4)!=0xFF)
    {
      n=leer_numero_tarjeta(punterotarjetas);
      info=EEPROM.read(punterotarjetas+4);
      if (n==n_leer)
      {
        EEPROM.write(punterotarjetas+4,((tipo<<4)|0x01));    //actualizar info
        escrito=1;
      }
      punterotarjetas=punterotarjetas+5;
    }
    else
    {
      punterotarjetas=punterotarjetas+4;
      for (int x=0; x<4; x++)
      {
        resto=n_leer%256;
        n_leer=n_leer/256;
        punterotarjetas--;
        EEPROM.write(punterotarjetas,resto);    //escribir numero tarjeta
      }
      punterotarjetas=punterotarjetas+4;
      EEPROM.write(punterotarjetas,((tipo<<4)|0x01)); //escribir info
      escrito=1;
    }
  }
  while(escrito==0);
}

void dardebajatarjetaEEPROM (unsigned long n_leer)
{
  unsigned char info;
  unsigned int punterotarjetas=EP_punteroTarjetas;
  unsigned long n;
  
  while (EEPROM.read(punterotarjetas+4)!=0xFF)
  {
    n=leer_numero_tarjeta(punterotarjetas);
    info=EEPROM.read(punterotarjetas+4);
    if (n==n_leer)
    {
      EEPROM.write(punterotarjetas+4,(info&0xFE));    //deshabilitar
    }
    punterotarjetas=punterotarjetas+5;
  }
}

unsigned long leer_numero_tarjeta (int punt)
{
  unsigned long leer=0;
 
  leer=EEPROM.read(punt);
  punt++;
  leer=EEPROM.read(punt)|(leer<<8);
  punt++;
  leer=EEPROM.read(punt)|(leer<<8);
  punt++;
  leer=EEPROM.read(punt)|(leer<<8);

  return leer;
}


