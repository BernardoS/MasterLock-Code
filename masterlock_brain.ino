#pragma region Configurações das bibliotecas

//Biblioteca do módulo de leitura da digital
#include <Adafruit_Fingerprint.h>
//Bibliteca do Firebase
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
//Bibliotecas para interação com a API
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
//Bibliotecas do módulo RFID
#include <SPI.h>
#include <MFRC522.h>
//Biblioteca do teclado matricial
#include <Keypad.h>
//Biblioteca do Display LCD
#include <LiquidCrystal_I2C.h>
//Bibliotecas do sistema de arquivos
#include "FS.h"
#include <LittleFS.h>
//Bibliotca para recuperação de tempo
#include "time.h"

#pragma endregion Configurações das bibliotecas

#pragma region Configurações da fechadura e configurações de rede

//Id da fechadura cadastrado no Firebase
String IdFechadura =  "ID_DA_FECHADURA";

//Configurações da rede e do Firebase
String WIFI_PASSWORD = "SENHA";
#define WIFI_SSID "REDE"
#define CHAVE_API "CHAVE_API"
#define URL_DO_BANCO "URL_DO_BANCO"

unsigned long controleReconexaoWifi = 0;

//Configurações do Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

#pragma endregion Configurações da fechadura e configurações de rede

#pragma region Configurações dos periféricos

//Configurações da recuperação de data e hora
String DiaEHora;
const char* ntpServer = "0.br.pool.ntp.org";
const long  gmtOffset_sec = -70800;
const int   daylightOffset_sec = 60000;

//Configurações do RFID
#define SS_PIN  5  
#define RST_PIN 27 
#define SIZE_BUFFER     18
#define MAX_SIZE_BLOCK  16

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

//Configurações do Teclado
#define NUM_LINHAS   4 
#define NUM_COLUNAS  3 
char teclas[NUM_LINHAS][NUM_COLUNAS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte pinos_linha[NUM_LINHAS] = {12, 14, 27, 26}; // GIOP18, GIOP5, GIOP17, GIOP16 - pinos de conexão das linhas do teclado
byte pinos_colunas[NUM_COLUNAS] = {25, 33, 32};  // GIOP4, GIOP0, GIOP2 - pinos de conexão das colunas do teclado
Keypad keypad = Keypad( makeKeymap(teclas), pinos_linha, pinos_colunas, NUM_LINHAS, NUM_COLUNAS );

//Confogurações do displayLCD
int colunasLCD = 16;
int linhasLCD = 2;
LiquidCrystal_I2C lcd(0x27, colunasLCD, linhasLCD);  

//Configurações do sensor de digital
const uint32_t password = 0x0;
String idsDigitais[16] = {"BERNARDO","MATHEUS","DONOVAN","JOAO","TAMASHIRO","IAGO","ISAIAS","THIAGO","JHONES","BEATRIZ","EZEQUIEL","BRUNO","MARCUS","JANAINA","PAULO","SAMUEL"};
Adafruit_Fingerprint fingerprintSensor = Adafruit_Fingerprint(&Serial2, password);

#pragma endregion Configurações dos periféricos

#pragma region Configurações genéricas

//Configurações genéricas
unsigned long controleLeituraPorta = 0;
unsigned long controleAtualizacaoPermissao = 0;
unsigned long controleUploadHistorico = 0;
String statusFechadura = "false";
int pinoFechadura = 4;
bool estaOnline = false;

//Configurações do menu
int menuStep = 0;
unsigned long controleExibicaoMenu = 0;

//Configurações para o armazenamento offline
#define FORMATAR_LITTLEFS_CASO_FALHE false

#pragma endregion Configurações genéricas

#pragma region Funcionalidades do sistema de arquivos

void deletarArquivo(fs::FS &fs, const char * path){
    Serial.printf("DELETANDO O  ARQUIVO: %s\r\n", path);
    if(fs.remove(path)){
        Serial.println("- ARQUIVO DELETADO");
    } else {
        Serial.println("- ERRO AO DELETAR");
    }
}

void escreverArquivo(fs::FS &fs, const char * path, const char * message){
    Serial.printf("ESCREVENDO NO ARQUIVO: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- ERRO AO ABRIR ARQUIVO PARA ESCREVER");
        return;
    }
    if(file.print(message)){
        Serial.println("- A ESCRITA  FOI UM SUCESSO");
    } else {
        Serial.println("- A ESCRITA FALHOU");
    }
    file.close();
}

bool achouTextoNoArquivo(fs::FS &fs, const char * path, const char * textoDeBusca){

  bool found = false; 
  Serial.printf("LENDO ARQUIVO: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- ERRO AO ABRIR ARQUIVO PARA REALIZAR LEITURA");
        return found;
    }

    
    Serial.println("- LENDO O ARQUIVO:");
    while(file.available()){  
        if(file.find(textoDeBusca)){
          Serial.println("ACHOU A PALAVRA:");
          Serial.println(textoDeBusca);
          found = true;
        }
    }
    file.close();

    return found;
}

void listarPastas(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listando pastas : %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- erro ao abrir pasta");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - não é uma pasta");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  PASTA : ");
            Serial.println(file.name());
            if(levels){
                listarPastas(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  ARQUIVO: ");
            Serial.print(file.name());
            Serial.print("\tTAMANHO: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

int contarPastas(fs::FS &fs, const char * dirname, uint8_t levels){

    int countDirectories = 0;

    File root = fs.open(dirname);
    if(!root){
        return countDirectories;
    }
    if(!root.isDirectory()){
        return countDirectories;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            if(levels){
                listarPastas(fs, file.name(), levels -1);
            }
            countDirectories++;
        } 
        file = root.openNextFile();
    }
    return countDirectories;
}

int contarArquivosNaPasta(fs::FS &fs, const char * dirname){
  int countFiles = 0;

  File root = fs.open(dirname);
    if(!root){
        Serial.println("- ERRO AO ABRIR PASTA");
        return countFiles;
    }
    if(!root.isDirectory()){
        Serial.println(" - NÃO É UMA PASTA");
        return countFiles;
    }

    File file = root.openNextFile();
    
    while(file){
        if(file.isDirectory() == false){
          countFiles++;
        }
        file = root.openNextFile();
    }
    return countFiles;
}

void criarPasta(fs::FS &fs, const char * path){
    Serial.printf("Criando pasta: %s\n", path);
    if(fs.mkdir(path)){
        Serial.println("Pasta criada");
    } else {
        Serial.println("Erro ao criar pasta");
    }
}

void lerArquivo(fs::FS &fs, const char * path){
    Serial.printf("LENDO ARQUIVO: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- ERRO AO ABRIR ARQUIVO PARA LEITURA");
        return;
    }

    
    Serial.println("- LEU DO ARQUIVO:");
    while(file.available()){  
        Serial.write(file.read());
    }
    Serial.println("");
    file.close();
}

String extrairTextoDoArquivo(const char* filename){
  auto file = LittleFS.open(filename, "r");
  size_t filesize = file.size();
  String data = file.readString();
  file.close();
  return data;
}

#pragma endregion Funcionalidades do sistema de arquivos

#pragma region Configurações inicais do Setup

void configurarDiretorios(){

  if(!LittleFS.begin(FORMATAR_LITTLEFS_CASO_FALHE)){
    Serial.println("Sistema de arquivo LittleFS montado com sucesso");
    return;
  }

  listarPastas(LittleFS, "/", 0);
  int qtdDirectories = contarPastas(LittleFS, "/", 0);
  Serial.print("Quantidade de diretórios: ");
  Serial.println(qtdDirectories);
  if(qtdDirectories == 0){
    criarPasta(LittleFS,"/permissoes");
    criarPasta(LittleFS,"/historico");
    listarPastas(LittleFS, "/", 0);
  }

}

void configurarSensorDeDigital(){
  fingerprintSensor.begin(57600);
  if(!fingerprintSensor.verifyPassword())
  {
    Serial.println(F("Não foi possível conectar ao sensor. Verifique a senha ou a conexão"));
    while(true);
  }

}

#pragma endregion Configurações inicais do Setup

#pragma region Configurações de rede e horário

String retornaDataEHoraAtual(){
  struct tm tempoEHora;
  char dataFormatada[64];
  String dataFormatadaEmString;

  if(!getLocalTime(&tempoEHora)){
    Serial.println("Erro ao recuperar data e hora.");
    return "Erro ao recuperar data";
  }

  strftime(dataFormatada, 64, "%d/%m/%Y %H:%M:%S", &tempoEHora);

  dataFormatadaEmString = dataFormatada;

  return dataFormatadaEmString;
}

void tentarConexaoWifi(){
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Conectando ao Wi-Fi");

  int tentativasMaximasDeConexao = 20; 
  int tentativas = 0;
  while (tentativas < 20) {
    if(WiFi.status() == WL_CONNECTED){
      Serial.println("CONECTADO!");
      tentativas = 20;
      Serial.println();
      Serial.print("Conectado a rede com o IP: ");
      Serial.println(WiFi.localIP());
      Serial.println();
      estaOnline = true;
    }
    Serial.print(".");
    delay(300);
    tentativas++;
  }
  Serial.println("");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void tentarConexaoFirebase(){
  config.api_key = CHAVE_API;
  config.database_url = URL_DO_BANCO;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
    signupOK = true;
  }
  else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

#pragma endregion Configurações de rede e horário

void setup() {
  pinMode(pinoFechadura, OUTPUT);
  Serial.begin(115200);

  tentarConexaoWifi();

  tentarConexaoFirebase();

  DiaEHora = retornaDataEHoraAtual();
  Serial.print("Iniciando o programa dia:");
  Serial.println(DiaEHora);  

  SPI.begin(); 
  rfid.PCD_Init(); 

  lcd.init();                   
  lcd.backlight();
  
  configurarSensorDeDigital();

  configurarDiretorios();
}

#pragma region Funções do modo offline

void registrarHistoricoOffline(String permissaoOffline, String operacao){
  String horario = retornaDataEHoraAtual();
  String historico = "{\"permissaoOffline\":\""+ permissaoOffline +"\",\"operacao\":\""+ operacao +"\",\"horario\":\""+ horario +"\"}";
  int qtdHistoricos = contarArquivosNaPasta(LittleFS,"/historico");
  qtdHistoricos++;
  String nomeArquivo = String(qtdHistoricos);
  String caminho = "/historico/"+nomeArquivo+".txt";

  const char * caminhoFormatado = caminho.c_str();
  const char * historicoFormatado = historico.c_str();
  escreverArquivo(LittleFS,caminhoFormatado,historicoFormatado);
  lerArquivo(LittleFS,caminhoFormatado);
}

void fazerUploadDosHistoricosOffline(){
  
  int qtdHistoricos = contarArquivosNaPasta(LittleFS,"/historico");
  if(qtdHistoricos > 0){
    Serial.println("Começando processo de upload de historicos registrados offline");
    WiFiClient client;
    HTTPClient http;

    for(int historicoAtual = 1;historicoAtual <= qtdHistoricos; historicoAtual++){
        
        String nomeDoArquivo = "/historico/"+ String(historicoAtual) + ".txt";
        Serial.println("Começando upload de:"+nomeDoArquivo);
        String dadosHistorico = extrairTextoDoArquivo(nomeDoArquivo.c_str());

        http.begin(client, "http://192.168.63.104:8080/historico-fechadura/cadastrar-offline");
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(dadosHistorico.c_str());
        
        String payload = "{}"; 

        if (httpResponseCode>0) {
          payload = http.getString();
          Serial.print("Cod. de resposta HTTP: ");
          Serial.println(httpResponseCode);
          Serial.println(payload);
          if(httpResponseCode == 404){
            lcd.clear(); 
            lcd.setCursor(0, 0);
            lcd.print("ACESSO NEGADO!");
            delay(2000);
          }else{
            deletarArquivo(LittleFS,nomeDoArquivo.c_str());
          }
        }else {
          Serial.print("Houve um erro na solicitação http: ");
          Serial.println(httpResponseCode);
        }
        delay(200);
    }
  }else{
    Serial.println("Não foi encontrado arquivos de histórico pendentes");
  }
}

void cadastrarPermissoesOffline(){
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://192.168.63.104:8080/permissoes/fechadura-lista");
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST("{\"idFechadura\":\""+ IdFechadura +"\"}");
        
  String payload = "{}"; 

  if (httpResponseCode>0) {
    payload = http.getString();
    const char* textMessage = payload.c_str();  
    Serial.print("Cod. de resposta HTTP: ");
    Serial.println(httpResponseCode);
    escreverArquivo(LittleFS,"/permissoes/permissoes.txt",textMessage);
  }else{
    Serial.print("Houve um erro na solicitação http: ");
    Serial.println(httpResponseCode);
  }
}

void alternarConexao(){
  lcd.clear(); 
  lcd.setCursor(0, 0);
  lcd.print("ALTERNANDO");
  lcd.setCursor(0,1);
  lcd.print("CONEXAO !");
  delay(500);
  if(estaOnline){
    WIFI_PASSWORD = "0";
  }else{
    WIFI_PASSWORD = "SENHA";
  }
  tentarConexaoWifi();
}

#pragma endregion Funções do modo offline

#pragma region Funções da fechadura

String lerStatusFechadura(){
  if (Firebase.RTDB.getString(&fbdo, "/fechadura/"+IdFechadura+"/EstaAberta")) {
      if (fbdo.dataType() == "string") {
        statusFechadura = fbdo.stringData();
        return statusFechadura;
      }
    }
    else {
      Serial.println(fbdo.errorReason());
      return "false";
    }
}

void abrirFechadura(){
    lcd.clear(); 
    lcd.setCursor(0, 0);
    lcd.print("DESBLOQUEANDO!");
    lcd.setCursor(0,1);
    lcd.print("BLOQUEIO EM 10s");
    digitalWrite(pinoFechadura, HIGH);
    delay(9000);
}

void trancarFechadura(){
  lcd.clear(); 
  lcd.setCursor(0, 0);
  lcd.print("FECHANDO A");
  lcd.setCursor(0,1);
  lcd.print("FECHADURA");
  delay(1000);
  digitalWrite(pinoFechadura, LOW);
  if(estaOnline == true){
    if (Firebase.RTDB.setString(&fbdo, "/fechadura/"+IdFechadura+"/EstaAberta", "false")) {
      Serial.println("Fechadura trancada!");
      
    }
    else {
      Serial.println("Falha ao trancar fechadura!");
      Serial.println("MOTIVO: " + fbdo.errorReason());
      trancarFechadura();
    }
  }
}
 
#pragma endregion Funções da fechadura

#pragma region Solicitações de abertura

void solicitarAberturaPorSenha(){
  String senhaNumerica = lerSenhaDoTeclado();
  if(senhaNumerica == "000000"){
    Serial.println("Erro ao coletar senha.");
  }else{
    if(estaOnline == true){
      Serial.print("Senha numérica: ");
      Serial.println(senhaNumerica);
      WiFiClient client;
      HTTPClient http;
      http.begin(client, "http://192.168.63.104:8080/permissoes/solicitar-entrada/senha");
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode = http.POST("{\"idFechadura\":\""+ IdFechadura +"\",\"senhaNumerica\":\""+ senhaNumerica +"\"}");
      
      String payload = "{}"; 

      if (httpResponseCode>0) {
        payload = http.getString();

        if(httpResponseCode == 404){
          Serial.println("Acesso negado!");
          lcd.clear(); 
          lcd.setCursor(0, 0);
          lcd.print("ACESSO NEGADO!");
          delay(2000);
        }
        if(httpResponseCode == 200){
          Serial.println("Acesso liberado!");
          lcd.clear(); 
          lcd.setCursor(0, 0);
          lcd.print("ACESSO LIBERADO!");
          delay(2000);
        }

      }else {
        Serial.print("Houve um erro na solicitação http: ");
        Serial.println(httpResponseCode);
      }
    }else{
      String textoDeBusca = IdFechadura +"|" +senhaNumerica;
      const char * textoDeBuscaFormatado = textoDeBusca.c_str();
      Serial.println(textoDeBuscaFormatado);
      bool acessoLiberado = achouTextoNoArquivo(LittleFS,"/permissoes/permissoes.txt",textoDeBuscaFormatado); 
      if(acessoLiberado == true){
        registrarHistoricoOffline(textoDeBusca,"Abertura liberada");
        abrirFechadura();
        trancarFechadura();
      }else{
        registrarHistoricoOffline(textoDeBusca,"Abertura bloqueada");
        lcd.clear(); 
        lcd.setCursor(0, 0);
        lcd.print("ACESSO NEGADO!");
        delay(2000);
      }
    }
    
  }
}

String lerSenhaDoTeclado(){
  lcd.clear(); 
  lcd.setCursor(0, 0);
  lcd.print("DIGITE A");
  lcd.setCursor(0,1);
  lcd.print("SENHA");
  String senhaNumerica = "000000";
  int quantidaDeDigitos = 0;
  while(quantidaDeDigitos < 6){
    char key = keypad.getKey();
    if (key) {
      Serial.println(key);
      delay(50);
      senhaNumerica[quantidaDeDigitos] = key;
      delay(50);
      quantidaDeDigitos++;
    }
  }
  return senhaNumerica;
}

void solicitarAberturaPorRFID(){
  lcd.clear(); 
  lcd.setCursor(0, 0);
  lcd.print("APROXIME O");
  lcd.setCursor(0,1);
  lcd.print("CARTAO/TAG");
  delay(2000);
  if (rfid.PICC_IsNewCardPresent()) { // new tag is available
    if (rfid.PICC_ReadCardSerial()) { // NUID has been readed
      String infoCartao = lerDadosDoCartao();
      if(infoCartao != "ERROR"){
        if(estaOnline == true){
          WiFiClient client;
          HTTPClient http;
          http.begin(client, "http://192.168.63.104:8080/permissoes/solicitar-entrada/rfid");
          http.addHeader("Content-Type", "application/json");
          int httpResponseCode = http.POST("{\"idFechadura\":\""+IdFechadura+"\",\"infoCartao\":\""+ infoCartao +"\"}");
          
          
          String payload = "{}"; 

          if (httpResponseCode>0) {
            payload = http.getString();
            if(httpResponseCode == 404){
              Serial.println("Acesso negado!");
              lcd.clear(); 
              lcd.setCursor(0, 0);
              lcd.print("ACESSO NEGADO!");
              delay(2000);
            }
            if(httpResponseCode == 200){
              Serial.println("Acesso liberado!");
              lcd.clear(); 
              lcd.setCursor(0, 0);
              lcd.print("ACESSO LIBERADO!");
              delay(2000);
            }
          }
          else {
            Serial.print("Houve um erro na solicitação http: ");
            Serial.println(httpResponseCode);
          }

        }else{
          String textoDeBusca = IdFechadura +"|" +infoCartao;
          const char * textoDeBuscaFormatado = textoDeBusca.c_str();
          Serial.println(textoDeBuscaFormatado);
          bool acessoLiberado = achouTextoNoArquivo(LittleFS,"/permissoes/permissoes.txt",textoDeBuscaFormatado); 
          if(acessoLiberado == true){
            registrarHistoricoOffline(textoDeBusca,"Abertura liberada");
            abrirFechadura();
            trancarFechadura();
          }else{
            registrarHistoricoOffline(textoDeBusca,"Abertura bloqueada");
            lcd.clear(); 
            lcd.setCursor(0, 0);
            lcd.print("ACESSO NEGADO!");
            delay(2000);
          }
        }
        
      }else{
        Serial.println("Erro ao ler cartão RFID");
      }
    }
  }
  //Encerra uma leitura - previne o travamento após a primeira leitura
  rfid.PICC_HaltA(); 
  rfid.PCD_StopCrypto1();  
}

String lerDadosDoCartao(){
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  byte buffer[SIZE_BUFFER] = {0};
  byte bloco = 1;
  byte tamanho = SIZE_BUFFER;
  status = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloco, &key, &(rfid.uid));
  if (status != MFRC522::STATUS_OK) {
    delay(1000);
    return "ERROR";
  }
  status = rfid.MIFARE_Read(bloco, buffer, &tamanho);
  if (status != MFRC522::STATUS_OK) {
    delay(1000);
    return "ERROR";
  }
  else{
      delay(1000);
  }

  String infoCartao = "AAA0000";
  for (int i = 0; i < MAX_SIZE_BLOCK; i++)
  {
    if(i >= 1 && i < 8){
      infoCartao[i-1] = buffer[i];
    }
  }
  Serial.println(" ");
  Serial.println("infoCartao :");
  Serial.println(infoCartao);
  return infoCartao;
}

void solicitarAberturaPorDigitial(){
  int posicaoDigital = verificarDigital();
  if(posicaoDigital>15){
    if(posicaoDigital == 30){
      lcd.clear(); 
      lcd.setCursor(0, 0);
      lcd.print("DIGITAL NAO");
      lcd.setCursor(0, 1);
      lcd.print("CADASTRADA!");
      delay(2000);
      if(estaOnline == true){
        WiFiClient client;
        HTTPClient http;
        http.begin(client, "http://192.168.63.104:8080/permissoes/solicitar-entrada/digital");
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST("{\"idFechadura\":\""+ IdFechadura +"\",\"idDigital\":\""+ "DIGITAL NAO CADASTRADA" +"\"}");
        String payload = "{}"; 

        if (httpResponseCode>0) {
          payload = http.getString();
        }else{
          Serial.print("Houve um erro na solicitação http:");
          Serial.println(httpResponseCode);
        }
      }else{
        String textoDeBuscaErro =  IdFechadura +"|"+"DIGITAL NAO CADASTRADA";
        registrarHistoricoOffline(textoDeBuscaErro,"Abertura bloqueada");
      }
    }
    Serial.println("Erro ao validar digital");
  }else{
    String codigoDigital = idsDigitais[posicaoDigital];
    Serial.print("Código da Digital: ");
    Serial.println(codigoDigital);
    if(estaOnline == true){
      WiFiClient client;
      HTTPClient http;
      http.begin(client, "http://192.168.63.104:8080/permissoes/solicitar-entrada/digital");
      http.addHeader("Content-Type", "application/json");
      int httpResponseCode = http.POST("{\"idFechadura\":\""+ IdFechadura +"\",\"idDigital\":\""+ codigoDigital +"\"}");
      
      String payload = "{}"; 

      if (httpResponseCode>0) {
        payload = http.getString();

        if(httpResponseCode == 404){
          Serial.println("Acesso negado!");
          lcd.clear(); 
          lcd.setCursor(0, 0);
          lcd.print("ACESSO NEGADO!");
          delay(2000);
        }

        if(httpResponseCode == 200){
          Serial.println("Acesso liberado!");
          lcd.clear(); 
          lcd.setCursor(0, 0);
          lcd.print("ACESSO LIBERADO!");
          delay(2000);
        }
      }
      else {
        Serial.print("Houve um erro na solicitação http: ");
        Serial.println(httpResponseCode);
      }
    }else{
      String textoDeBusca = IdFechadura +"|" +codigoDigital;
      const char * textoDeBuscaFormatado = textoDeBusca.c_str();
      Serial.println(textoDeBuscaFormatado);
      bool acessoLiberado = achouTextoNoArquivo(LittleFS,"/permissoes/permissoes.txt",textoDeBuscaFormatado); 
      if(acessoLiberado == true){
        registrarHistoricoOffline(textoDeBusca,"Abertura liberada");
        abrirFechadura();
        trancarFechadura();
      }else{
        registrarHistoricoOffline(textoDeBusca,"Abertura bloqueada");
        lcd.clear(); 
        lcd.setCursor(0, 0);
        lcd.print("ACESSO NEGADO!");
        delay(2000);
      }
      
    }
    
  }
  
}

int verificarDigital(){
  lcd.clear(); 
  lcd.setCursor(0, 0);
  lcd.print("APROXIME A");
  lcd.setCursor(0,1);
  lcd.print("DIGITAL");

  //Espera até pegar uma imagem válida da digital
  while (fingerprintSensor.getImage() != FINGERPRINT_OK);

  //Converte a imagem para o padrão que será utilizado para verificar com o banco de digitais
  if (fingerprintSensor.image2Tz() != FINGERPRINT_OK)
  {
    //Se chegou aqui deu erro, então abortamos os próximos passos
    Serial.println(F("Erro image2Tz"));
    return 20;
  }
  
  //Procura por este padrão no banco de digitais
  if (fingerprintSensor.fingerFastSearch() != FINGERPRINT_OK)
  {
    //Se chegou aqui significa que a digital não foi encontrada
    Serial.println(F("Digital não encontrada"));
    return 30;
  }

  //Se chegou aqui a digital foi encontrada
  //Mostramos a posição onde a digital estava salva e a confiança
  //Quanto mais alta a confiança melhor
  Serial.print(F("Digital encontrada com confiança de "));
  Serial.print(fingerprintSensor.confidence);
  Serial.print(F(" na posição "));
  Serial.println(fingerprintSensor.fingerID);
  int posicaoSensor = fingerprintSensor.fingerID;
  if(posicaoSensor > 0 && posicaoSensor <= 10){
    return 0;
  }else if(posicaoSensor > 10 && posicaoSensor <= 20){
    return 1;
  }else if(posicaoSensor > 20 && posicaoSensor <= 30){
    return 2;
  }else if(posicaoSensor > 30 && posicaoSensor <= 40){
    return 3;
  }else if(posicaoSensor > 40 && posicaoSensor <= 50){
    return 4;
  }else if(posicaoSensor > 50 && posicaoSensor <= 60){
    return 5;
  }else if(posicaoSensor > 60 && posicaoSensor <= 70){
    return 6;
  }else if(posicaoSensor > 70 && posicaoSensor <= 80){
    return 7;
  }else if(posicaoSensor > 80 && posicaoSensor <= 90){
    return 8;
  }else if(posicaoSensor > 90 && posicaoSensor <= 100){
    return 9;
  }else if(posicaoSensor > 100 && posicaoSensor <= 110){
    return 10;
  }else if(posicaoSensor > 110 && posicaoSensor <= 120){
    return 11;
  }else if(posicaoSensor > 120 && posicaoSensor <= 130){
    return 12;
  }else if(posicaoSensor > 130 && posicaoSensor <= 140){
    return 13;
  }else if(posicaoSensor > 140 && posicaoSensor <= 150){
    return 14;
  }else if(posicaoSensor > 150 && posicaoSensor <= 160){
    return 15;
  }
}
#pragma endregion Solicitações de abertura

#pragma region Funções do Menu

void exibirMenu(){ 
  if((millis() - controleExibicaoMenu > 2000 || controleExibicaoMenu == 0)){
      controleExibicaoMenu = millis();
      switch (menuStep) {
        case 0:
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("MASTER LOCK!");
          lcd.setCursor(0,1);
          if(estaOnline == false){
            lcd.print("MODO OFFLINE!");
          }else{
            lcd.print("BEM-VINDO!");
          }
          menuStep++;
          break;
        case 1:
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("SELECIONE A");
          lcd.setCursor(0,1);
          lcd.print("ENTRADA:");
          menuStep++;
          break;
        case 2:
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("1-RFID| 2-SENHA");
          lcd.setCursor(0,1);
          lcd.print("3-DIGITAL");
          menuStep = 0;
          break;
        default:
          lcd.clear();
          break;
      }
  }
}

void fazerLeituraDoMenu(){
  exibirMenu();
  char key = keypad.getKey();
  if (key) {
    lcd.clear(); 
    lcd.setCursor(0, 0);
    lcd.print("ENTRADA");
    lcd.setCursor(0,1);
    lcd.print("SOLICITADA !");
    delay(100);
    switch (key){
      case '1':
        solicitarAberturaPorRFID();
        break;
      case '2':
        solicitarAberturaPorSenha();
        break;
      case '3':
        solicitarAberturaPorDigitial();
        break;
      case '#':
        cadastrarPermissoesOffline();
        break;
      case '*':
        alternarConexao();
        break;
      default:
        Serial.println(F("Opção inválida"));
        break;
    }
    delay(1000);
  }
}

#pragma endregion Funções do Menu

void loop() {
  if(WiFi.status() == WL_CONNECTED){
    estaOnline = true;
    if(millis() - controleUploadHistorico > 60000 || controleUploadHistorico == 0){
      controleUploadHistorico = millis();
      int qtdHistoricosPendentes =  contarArquivosNaPasta(LittleFS,"/historico");
      if(qtdHistoricosPendentes > 0){
        fazerUploadDosHistoricosOffline();
      }
    }
  }else{
    estaOnline = false;
  }

  if(estaOnline == true){
      //checando se a conexão com o Firebase está disponível
      if (Firebase.ready() && signupOK ) {
      //checando o status da fechadura no banco, a cada dois segundos
        if((millis() - controleLeituraPorta > 500 || controleLeituraPorta == 0)){
          controleLeituraPorta = millis();
          if(lerStatusFechadura() == "true"){
            Serial.println("Abrindo fechadura...");
            abrirFechadura();
            trancarFechadura();
          }
        }
        if(millis() - controleAtualizacaoPermissao > 3600000 || controleAtualizacaoPermissao == 0){
          controleAtualizacaoPermissao = millis();
          cadastrarPermissoesOffline();
        }
      }
  }else{
    if(millis() - controleReconexaoWifi > 60000 || controleReconexaoWifi == 0){
      controleReconexaoWifi = millis();
      tentarConexaoWifi();
      if(WiFi.status() == WL_CONNECTED){
        tentarConexaoFirebase();
      }
    }
  }
  fazerLeituraDoMenu();
}