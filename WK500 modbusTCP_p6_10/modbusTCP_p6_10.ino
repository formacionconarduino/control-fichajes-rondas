//WK0500 -> ATMEGA 2560 -> Graba nº esclavos en EPROM (4k -> 0 a 0x0FFF)

#define debug

#include <SPI.h>
#include <Ethernet.h>
#include "Mudbus.h"
#include <EEPROM.h>

//#include <SimpleModbusMaster.h>       //Master ModBuus RTU
#include <Smm_Juan.h>

#include <Wire.h>            //para el I2C del reloj
#include <Rtc_Pcf8563.h>

#define EP_punteroesclavos 20

#define intervalotiempo 1000

Mudbus MbTCP;

Packet remotos[1];
packetPointer remoto=&remotos[0];
unsigned int regMbRTU[25];
unsigned int resultadoRTU=0;
boolean cambio=true,comandoRTU=false,lecturaactivada=false;

Rtc_Pcf8563 rtc;

unsigned char hora, minutos, segundos;
unsigned char dia, dia_semana, mes, centuria=0, ano;
unsigned int punteroesclavos=EP_punteroesclavos;
unsigned int indiceesclavos=EP_punteroesclavos+1, indicealmacen;
unsigned int indicedevolverHR=EP_punteroesclavos+1, indicealmacen2;
unsigned int almacen [10][8];
unsigned long tiempolectura, nuevotiempo;

int z=0;

void setup ()
{
  uint8_t mac[]= {0x90, 0xa2, 0xda, 0x00, 0x51, 0x06};
  uint8_t ip[]={10, 0, 0, 166};
  uint8_t gateway[]={192,168,1,1};
  uint8_t subnet[]={255,255,0,0};
  Ethernet.begin(mac, ip, gateway, subnet);
  
  remoto->register_array = regMbRTU;      //inicializacion ModBus RTU
  modbus_configure(19200,1000,5,1,0,remotos,1);
  
  MbTCP.R[0]=0xFFFF;
  
  if (EEPROM.read(EP_punteroesclavos)==0xFF) { EEPROM.write(EP_punteroesclavos,0x00); }
  else { punteroesclavos=EEPROM.read(EP_punteroesclavos)+EP_punteroesclavos; }
  
  Wire.begin();      //inicia I2C
  //rtc.initClock();    //inicializo reloj
  tiempolectura=millis();
  
  #ifdef debug
    Serial.begin(9600);
  #endif
}

void loop ()
{
  if (!comandoRTU)
  {
    MbTCP.Run();
    if (MbTCP.R[0]!=0xFFFF)
    {
      ejecutacomandoTCP();
      MbTCP.R[0]=0xFFFF;
    }
    else
    {
      nuevotiempo=millis();
      if (nuevotiempo > (tiempolectura+intervalotiempo))    //si hay muchos esclavos disminuir el tiempo
      {
        if (EEPROM.read(EP_punteroesclavos)!=0)
        {
          MbTCP.R[1]=(EEPROM.read(indiceesclavos)<<8);
          MbTCP.R[0]=0x002F;
          ejecutacomandoTCP();
          MbTCP.R[0]=0xFFFF;
          tiempolectura=nuevotiempo;
        }
      }
      else
      {
        if (tiempolectura > (nuevotiempo+intervalotiempo))    //caso desborde temporizador
        { 
          tiempolectura=nuevotiempo;
        }             
      }
    }
  }
/* --------------------------------------------------------------------
  if (regMbRTU[0]!=0xFFFF)    //Esta versión funciona... no borrar
  {
    resultadoRTU=modbus_update(remotos,true);
    if (resultadoRTU!=0) regMbRTU[0]=0xFFFF;
  }
--------------------------------------------------------------------- */
  if (comandoRTU)     //tambien funciona  -> Usar esta para poder leer tarjeta 
  {
    resultadoRTU=modbus_update(remotos,true);
    if (resultadoRTU!=0)        //0->en proceso, 1->OK, 2->error
    {
      comandoRTU=false;
      
      #ifdef debug
        Serial.print("RTU: ");
        for (int b=0; b<10; b++)
        {
          Serial.print(regMbRTU[b],HEX);
          Serial.print(" ");
        }
        Serial.println(" ");
      #endif
  
      if ((regMbRTU[6]!=0)&&(regMbRTU[7]!=0))  //hay tarjeta que leer
      {
        
        indicealmacen=indiceesclavos-(EP_punteroesclavos+1);
        almacen[indicealmacen][0]=EEPROM.read(indiceesclavos);
        almacen[indicealmacen][1]=regMbRTU[6];  //numero tarjeta palabra alta
        almacen[indicealmacen][2]=regMbRTU[7];  //numero tarjeta palabra baja
        almacen[indicealmacen][3]=regMbRTU[8];  //informacion extra
        regMbRTU[6]=regMbRTU[7]=regMbRTU[8]=0;
        
        //Leer hora y fecha
        rtc.getTime();
        hora=rtc.getHour();
        minutos=rtc.getMinute();
        segundos=rtc.getSecond();
        rtc.getDate();
        dia=rtc.getDay();
        dia_semana=rtc.getWeekday();
        mes=rtc.getMonth();
        ano=rtc.getYear();
        almacen[indicealmacen][4]=(hora<<8)|minutos;
        almacen[indicealmacen][5]=(segundos<<8)|dia_semana;
        almacen[indicealmacen][6]=(dia<<8)|mes;
        if (centuria==0) { almacen[indicealmacen][7]=ano+2000; }
        else  { almacen[indicealmacen][7]=ano+1900; }
        
        MbTCP.R[0]=0x002c;                        //borrar registros RTU en esclavo
        MbTCP.R[1]=(EEPROM.read(indiceesclavos)<<8);    //¿Hace falta?
        ejecutacomandoTCP();
        MbTCP.R[0]=0xFFFF;
        
      }
      
      if (lecturaactivada)
      {
        lecturaactivada=false;
        indiceesclavos++;
        if (indiceesclavos>punteroesclavos){   indiceesclavos=EP_punteroesclavos+1;      }
      
      }
    }
  }  
}

void ejecutacomandoTCP()
{
  unsigned char x;
  
  switch(MbTCP.R[0]&0x00FF)          //De momento no se maneja el identificador del terminal (PC, tablet, etc.), solo comando TCP
  {  
    case 0x20:  //Abrir de forma remota    //Comando TCP
                regMbRTU[0]=0x20;          //Comando RTU en posicion 0
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;
                comandoRTU=true;
                break;      
  
    case 0x21:  //Dar de alta tarjeta      //Comando TCP
                regMbRTU[0]=0x21;          //Comando RTU en posicion 0 
                regMbRTU[1]=MbTCP.R[2];    //numero tarjeta -> empieza por byte alto
                regMbRTU[2]=MbTCP.R[3];
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;  
                comandoRTU=true;
                break;
     case 0x22:  //Dar de alta tarjeta maestra   //Comando TCP
                regMbRTU[0]=0x22;          //Comando RTU en posicion 0
                regMbRTU[1]=MbTCP.R[2];    //numero tarjeta -> empieza por byte alto
                regMbRTU[2]=MbTCP.R[3];
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;  
                comandoRTU=true;
                break;         
    case 0x23:  //Dar de alta tarjeta super maestra   //Comando TCP
                regMbRTU[0]=0x23;          //Comando RTU en posicion 0
                regMbRTU[1]=MbTCP.R[2];    //numero tarjeta -> empieza por byte alto
                regMbRTU[2]=MbTCP.R[3];
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5; 
                comandoRTU=true; 
                break;   
    case 0x24:  //Dar de baja tarjeta      //Comando TCP
                regMbRTU[0]=0x24;          //Comando RTU en posicion 0
                regMbRTU[1]=MbTCP.R[2];    //numero tarjeta -> empieza por byte alto
                regMbRTU[2]=MbTCP.R[3];
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;  
                comandoRTU=true;
                break;
   
    case 0x25:  //Bloquear puerta          //Comando TCP
                regMbRTU[0]=0x25;          //Comando RTU en posicion 0
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;
                comandoRTU=true;
                break;  
    case 0x26:  //Desbloquear puerta        //Comando TCP
                regMbRTU[0]=0x26;           //Comando RTU en posicion 0
                remoto->id=MbTCP.R[1]>>8;   //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;
                comandoRTU=true;
                break;       
    case 0x27:  //Consultar puerta         //Comando TCP
                break;   
    case 0x28:  //Borrar cerradura         //Comando TCP
                regMbRTU[0]=0x28;          //Comando RTU en posicion 0
                remoto->id=MbTCP.R[1]>>8;  //direccion modbus esclavo RTU
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=5;
                comandoRTU=true;
                break;       
                
    case 0x29:  //Dar de alta esclavo      //Comando TCP
                punteroesclavos++;
                EEPROM.write(punteroesclavos, MbTCP.R[1]>>8);
                x=EEPROM.read(EP_punteroesclavos);
                x++;
                EEPROM.write(EP_punteroesclavos,x);   
                break;
    case 0x2A:  //Pasar al terminal número remotos total y los que son
                for (x=EP_punteroesclavos; x<=punteroesclavos; x++)
                {
                    MbTCP.R[x]=EEPROM.read(x);
                }   //La informacion estara a partir del MbTCP.R 20
                break;
    case 0x2B:  //Actualizar hora en el reloj
                rtc.initClock();    //inicializo reloj
                hora=MbTCP.R[2]>>8;
                minutos=MbTCP.R[2]&0x00ff;
                segundos=MbTCP.R[3]>>8;
                dia_semana=MbTCP.R[3]&0x00ff;
                dia=MbTCP.R[4]>>8;
                mes=MbTCP.R[4]&0x00ff;
                centuria=MbTCP.R[5]>>8;                //0 es 2000, 1 es 1900
                ano=MbTCP.R[5]&0x00ff;                    
                rtc.setTime(hora,minutos,segundos);
                rtc.setDate(dia,dia_semana,mes,centuria,ano);
                break;
    case 0x2C:  //Borra registros ModBus en esclavo    //Comando TCP
                regMbRTU[0]=0x2C;
                remoto->id=MbTCP.R[1]>>8;              //direccion modbus esclavo RTU 
                remoto->function=PRESET_MULTIPLE_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=10;
                comandoRTU=true;
                break; 
                
    case 0x2E:  //Devolver tarjeta leida al terminal PC -> recoger del array almacen y dejar en HR
                if (EEPROM.read(EP_punteroesclavos)!=0)
                {
                  indicealmacen2=indicedevolverHR-(EP_punteroesclavos+1);
                  MbTCP.R[6]=almacen[indicealmacen2][1];    //numero tarjeta
                  MbTCP.R[7]=almacen[indicealmacen2][2];    //numero tarjeta
                  MbTCP.R[8]=almacen[indicealmacen2][3];    //informacion extra
                  MbTCP.R[9]=almacen[indicealmacen2][4];    //informacion hora
                  MbTCP.R[10]=almacen[indicealmacen2][5];   //informacion hora
                  MbTCP.R[11]=almacen[indicealmacen2][6];   //informacion fecha
                  MbTCP.R[12]=almacen[indicealmacen2][7];   //informacion fecha
                  MbTCP.R[13]=almacen[indicealmacen2][0];   //numero esclavo-remoto
                  MbTCP.R[14]=indicedevolverHR;
                  indicedevolverHR++;
                  if (indicedevolverHR>punteroesclavos)
                  {
                    indicedevolverHR=EP_punteroesclavos+1;
                  }
                }                 
                break;            
    case 0x2F:  //Leer ultima tarjeta pasada en remoto   //Comando TCP
                remoto->id=MbTCP.R[1]>>8;                //direccion modbus esclavo RTU 
                remoto->function=READ_HOLDING_REGISTERS;
                remoto->address=0;
                remoto->no_of_registers=10;
                comandoRTU=true;
                lecturaactivada=true;
                break;                  
  }
  #ifdef debug
    Serial.print("TCP: ");
    for (int b=0; b<15; b++)
    {
      Serial.print(MbTCP.R[b],HEX);
      Serial.print(" ");
    }
    Serial.println(" ");
  #endif
}

