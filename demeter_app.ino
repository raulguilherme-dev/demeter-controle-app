#include <ESP8266WiFi.h> 
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Arduino_JSON.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <String.h>

String resposta;
String jsonString;
String datetime;
String data;
String horario;
String tipo;
String novoHorario_string;

double valor;

int horaRecebida;
int minutoRecebido;
int fluxo;
int vazao;
int fluxoAcumulado;
int* novoHorario;
int new_h;
int new_m;

const int pino_led = 15;


// Função para acionar um LED em caso de erro
void acionaLed() {
  for (int i=0; i<5; i++) {
    digitalWrite(pino_led, HIGH);
    delay(500);
    digitalWrite(pino_led, LOW);
    delay(500);
  }
}

// Converte o valor lido pelo sensor de fluxo para litro
double totalLitro(int fluxo) {
  vazao = (fluxo * 2.25);
  fluxoAcumulado = (vazao / 1000);
  return fluxoAcumulado;
}

//Função para icrementar em dois minutos a partir da hora atual
//Usado para calcular antes de enviar uma nova requisição na API após o fechamento da valvúla 
int* incrementaHora(int h, int m) {
   m = m + 2;
   if (m >= 60) {
    m = m - 60;
    h++;
    if (h >= 24) {
      h = h - 24;
    }
   }
   int* novo = new int[2];
   novo[0] = h;
   novo[1] = m;
   return novo;
}

// Informações da rede
const char* ssid = "[Nome da Rede]";
const char* password = "[Senha da Rede]";

void setup(void) {
  Serial.begin(9600);
  delay(10);
  Serial.println('\n');

  pinMode(5, INPUT); // Pino Sensor de Fluxo
  pinMode(14, OUTPUT); // Pino Válvula
  pinMode(pino_led, OUTPUT); // Pino LED
  attachInterrupt(digitalPinToInterrupt(5), incInpulso, RISING);

  // Aguarda a conexão WiFi
  Serial.printf("Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // Faz um get request na World Api para obter dados de data e hora atual
  resposta = httpGETRequest("http://worldtimeapi.org/api/timezone/America/Sao_Paulo");
  JSONVar objeto = JSON.parse(resposta);

  // Converte apenas o dado de interesse ["datetime"] para String para tornar mais fácil a manipulação
  datetime = JSON.stringify(objeto["datetime"]);
  
  setTime( //Define data e horário atual a partir da resposta da worldtimeapi
    datetime.substring(12, 14).toInt(), // hora
    datetime.substring(15, 17).toInt(), // minutos
    datetime.substring(18, 20).toInt(), // segundos
    datetime.substring(9, 11).toInt(), // dia
    datetime.substring(6, 8).toInt(), // mes
    datetime.substring(1, 5).toInt() // ano
  ); //Foi definido dessa forma para evitar muitas variáveis apenas para essa função
  
}

void loop() {
  // Obtem a hora atual
  time_t current_time = now();

  // Converte a hora em uma estrutura tm
  struct tm *tm = localtime(&current_time);

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    //Faz um GET request para pegar a solicitação mais recente na API
    resposta = httpGETRequest("[ENDPOINT]");
    JSONVar objeto = JSON.parse(resposta);

    // Armazena os valores recebidos pela api
    horario = JSON.stringify(objeto["horario"]);
    tipo = JSON.stringify(objeto["tipo"]);
    valor = JSON.stringify(objeto["valor"]).toDouble();

    //Remove as aspas "" da variavel tipo
    tipo = tipo.substring(1, (tipo.length() -1));

    Serial.println("Novo horário de acionamento: " +  horario);

    //Extrai a hora e minuto recebido da variável horário e converte para Inteiro 
    horaRecebida = horario.substring(1, 3).toInt();
    minutoRecebido = horario.substring(4, 6).toInt();

    if (tm->tm_hour == horaRecebida && tm->tm_min == minutoRecebido) { //Aguarda o horário recebido através da chamada da API
      if (!(tipo.equalsIgnoreCase("tempo") || tipo.equalsIgnoreCase("fluxo"))) { //Verifica se o tipo de acionamento é válido
        Serial.println("Não foi possível identificar o tipo de irrigação");
        acionaLed();
      } else {
        Serial.println("Valvúla acionada");
        digitalWrite(14, HIGH); //Aciona a valvúla
        if (tipo.equalsIgnoreCase("tempo")){
          delay(valor * 60000); 
        } else if (tipo.equalsIgnoreCase("fluxo")) {
          while (totalLitro(fluxo) < valor) {
            Serial.print(totalLitro(fluxo));
            Serial.print(" -> ");
            Serial.println(valor);
          }
        }
        Serial.println("Valvúla fechada");
        digitalWrite(14, LOW); // Fecha a valvúla

        time_t current_time = now();

        struct tm *tm = localtime(&current_time);
        
        novoHorario = incrementaHora(tm->tm_hour, tm->tm_min);

        new_h = novoHorario[0];
        new_m = novoHorario[1];

        //Converte o novo horário de acionamento para String
        novoHorario_string = String(new_h) + ":" + String(new_m);

        // Armazena os valores em um JSON
        StaticJsonDocument<200> doc;
        doc["valor"] = 1;
        doc["tipo"] = tipo; // O tipo de acionamento será igual ao mais recente recebido pela API
        doc["horario"] = novoHorario_string;
    
        String requestBody;
        serializeJson(doc, requestBody);
        
        // Faz um POST request com as novas informações para manter o sistema acionando automaticamente a cada dois minutos
        httpPOSTRequestJSON("[ENDPOINT]", requestBody);
        delay(5000);
      }
    } else {
      Serial.print("Hora atual: ");
      Serial.print(tm->tm_hour);
      Serial.print(":");
      Serial.print(tm->tm_min);
      Serial.print(":");
      Serial.println(tm->tm_sec);
      delay(5000);
    }

    delay(5000);
  } else {
    acionaLed();
    ESP.deepSleep(5 * 60000000);
  }
}


//Função de GET request
String httpGETRequest(const char* serverName) {
  WiFiClient client;
  HTTPClient http;
    
  // Your IP address with path or Domain name with URL path 
  http.begin(client, serverName);
  
  // Send HTTP POST request
  int httpResponseCode = http.GET();
  
  String payload = "{}"; 
  
  if (httpResponseCode>0) {
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    acionaLed();
  }
  // Free resources
  http.end();

  return payload;
}

void httpPOSTRequestJSON(String s, String r) {

  WiFiClient client;
  HTTPClient http;
  
  String site = s;
  String requestBody = r;
  Serial.println(requestBody);

  Serial.print("[HTTP] begin...\n");
  // configure traged server and url
  http.begin(client, site); //HTTP
  http.addHeader("Content-Type", "application/json");

  Serial.print("[HTTP] POST...\n");
  // start connection and send HTTP header and body
  int httpCode = http.POST(requestBody);

  // httpCode will be negative on error
  do { 
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] POST... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        const String& payload = http.getString();
        Serial.println("received payload:\n<<");
        Serial.println(payload);
        Serial.println(">>");
      }
    } else {
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
      acionaLed();
    }
  } while (httpCode != 200);
}

ICACHE_RAM_ATTR void incInpulso() {
  fluxo++;
}
