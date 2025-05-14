#include <stdio.h>               // Biblioteca padrão para entrada e saída
#include <string.h>              // Biblioteca manipular strings
#include <stdlib.h>              // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)
#include "hardware/pwm.h"        // Biblioteca da Raspberry Pi Pico para manipulação de PWM

#include "pico/stdlib.h"         // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"        // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "pico/cyw43_arch.h"     // Biblioteca para arquitetura Wi-Fi da Pico com CYW43  

#include "lwip/pbuf.h"           // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"            // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"          // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "Wilma"
#define WIFI_PASSWORD "99896898"

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN   // GPIO do CI CYW43
#define LED_BLUE_PIN 12                 // GPIO12 - LED azul
#define LED_GREEN_PIN 11                // GPIO11 - LED verde
#define LED_RED_PIN 13                  // GPIO13 - LED vermelho
#define BUZZER_PIN 21                   // GPIO21 - Buzzer

uint8_t red_led_brightness = 0; // 0 a 255
uint8_t green_led_brightness = 0; // 0 a 255
uint8_t blue_led_brightness = 0; // 0 a 255

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Leitura da temperatura interna
float temp_read(void);

// Tratamento do request do usuário
void user_request(char **request);

// Função principal
int main()
{
    //Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    gpio_led_bitdog();

    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    // Inicializa o conversor ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true)
    {
        /* 
        * Efetuar o processamento exigido pelo cyw43_driver ou pela stack TCP/IP.
        * Este método deve ser chamado periodicamente a partir do ciclo principal 
        * quando se utiliza um estilo de sondagem pico_cyw43_arch 
        */
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);      // Reduz o uso da CPU
    }

    //Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

// Inicializa o PWM para controle de LEDs
void pwm_init_led(uint gpio_pin) {
    gpio_set_function(gpio_pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(gpio_pin);
    pwm_set_wrap(slice_num, 255);              // 8 bits (0-255)
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio_pin), 0);
    pwm_set_enabled(slice_num, true);
}

void tocar_alarme() {
    for (int j = 0; j < 5; j++) {  // 5 beeps
        for (int i = 0; i < 100; i++) {
            gpio_put(BUZZER_PIN, 1);
            sleep_us(500);  // Meio ciclo de 1kHz
            gpio_put(BUZZER_PIN, 0);
            sleep_us(500);
        }
        sleep_ms(200);  // Pausa entre os beeps (200ms)
    }
}

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void){
    
    pwm_init_led(LED_GREEN_PIN);  // Inicializa LED verde com PWM
    
    pwm_init_led(LED_BLUE_PIN);  // Inicializa LED azul com PWM
    
    pwm_init_led(LED_RED_PIN);  // Inicializa LED vermelho com PWM

    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);  // Começa desligado

}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário - digite aqui
void user_request(char **request){

    if (strstr(*request, "GET /blue_up") != NULL)
    {
        if (blue_led_brightness <= 245) blue_led_brightness += 10;  // aumenta brilho
    }
    else if (strstr(*request, "GET /blue_down") != NULL)
    {
        if (blue_led_brightness >= 10) blue_led_brightness -= 10;   // diminui brilho
    }
    else if (strstr(*request, "GET /green_up") != NULL)
    {
        if (green_led_brightness <= 245) green_led_brightness += 10;  // aumenta brilho
    }
    else if (strstr(*request, "GET /green_down") != NULL)
    {
        if (green_led_brightness >= 10) green_led_brightness -= 10;   // diminui brilho
    }
    else if (strstr(*request, "GET /on") != NULL)
    {
        cyw43_arch_gpio_put(LED_PIN, 1);
    }
    else if (strstr(*request, "GET /off") != NULL)
    {
        cyw43_arch_gpio_put(LED_PIN, 0);
    } else if (strstr(*request, "GET /red_up") != NULL)
    {
        if (red_led_brightness <= 245) red_led_brightness += 10;  // aumenta brilho
    }
    else if (strstr(*request, "GET /red_down") != NULL)
    {
        if (red_led_brightness >= 10) red_led_brightness -= 10;   // diminui brilho
    } else if (strstr(*request, "GET /alarm") != NULL)
    {
        tocar_alarme();
    }
        
    uint slice_red = pwm_gpio_to_slice_num(LED_RED_PIN);
    uint channel_red = pwm_gpio_to_channel(LED_RED_PIN);

    uint slice_green = pwm_gpio_to_slice_num(LED_GREEN_PIN);
    uint channel_green = pwm_gpio_to_channel(LED_GREEN_PIN);

    uint slice_blue = pwm_gpio_to_slice_num(LED_BLUE_PIN);
    uint channel_blue = pwm_gpio_to_channel(LED_BLUE_PIN);

    // Atualiza o nível de PWM para os LEDs
    pwm_set_chan_level(slice_red, channel_red, red_led_brightness);
    pwm_set_chan_level(slice_green, channel_green, green_led_brightness);
    pwm_set_chan_level(slice_blue, channel_blue, blue_led_brightness);

};

// Leitura da temperatura interna
float temp_read(void){
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    float temperature = 27.0f - ((raw_value * conversion_factor) - 0.706f) / 0.001721f;
        return temperature;
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);
    
    // Leitura da temperatura interna
    float temperature = temp_read();

    // Cria a resposta HTML
    char html[1024]; // Buffer para armazenar a resposta HTML

    // Instruções html do webserver
    snprintf(html, sizeof(html), // Formatar uma string e armazená-la em um buffer de caracteres
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title> Painel de Controle </title>\n"
             "<style>\n"
             "body { background-color:rgb(251, 187, 195); font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
             "h1 { font-size: 64px; margin-bottom: 30px; }\n"
             "button { background-color: LightGray; font-size: 36px; margin: 10px; padding: 20px 40px; border-radius: 10px; }\n"
             ".temperature { font-size: 48px; margin-top: 30px; color: #333; }\n"
             "</style>\n"
             "</head>\n"
             "<body>\n"
             "<h1> Painel de Controle</h1>\n"
             "<form action=\"./blue_up\"><button>Aumentar Azul</button></form>\n"
             "<form action=\"./blue_down\"><button>Diminuir Azul</button></form>\n"
             "<form action=\"./green_up\"><button>Aumentar Verde</button></form>\n"
             "<form action=\"./green_down\"><button>Diminuir Verde</button></form>\n"
             "<form action=\"./red_up\"><button>Aumentar Vermelho</button></form>\n"
             "<form action=\"./red_down\"><button>Diminuir Vermelho</button></form>\n"
             "<form action=\"./alarm\"><button>Tocar Alarme</button></form>\n" 
             //"<form action=\"./festa\"><button>Modo Festa</button></form>\n"
             //"<form action=\"./noturno\"><button>Modo Noturno</button></form>\n"
             //"<form action=\"./temporizador\"><button>Temporizador</button></form>\n"
             "<p class=\"temperature\">Temperatura Interna: %.2f &deg;C</p>\n"
             "</body>\n"
             "</html>\n", temperature);

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    //libera memória alocada dinamicamente
    free(request);
    
    //libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}