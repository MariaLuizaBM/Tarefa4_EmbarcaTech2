#include <stdio.h>               // Biblioteca padrão para entrada e saída
#include <string.h>              // Biblioteca manipular strings
#include <stdlib.h>              // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)
#include "hardware/pwm.h"        // Biblioteca da Raspberry Pi Pico para manipulação de PWM
#include "hardware/clocks.h"     // Biblioteca da Raspberry Pi Pico para manipulação de clocks

#include "pico/stdlib.h"         // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"        // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "pico/cyw43_arch.h"     // Biblioteca para arquitetura Wi-Fi da Pico com CYW43  

#include "lwip/pbuf.h"           // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"            // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"          // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN   // GPIO do CI CYW43
#define LED_BLUE_PIN 12                 // GPIO12 - LED azul
#define LED_GREEN_PIN 11                // GPIO11 - LED verde
#define LED_RED_PIN 13                  // GPIO13 - LED vermelho
#define BUZZER_PIN 21                   // GPIO21 - Buzzer

absolute_time_t desligar_em = {0};  // Tempo futuro para desligar os LEDs
bool temporizador_ativo = false;

uint8_t red_led_brightness = 0; // 0 a 255
uint8_t green_led_brightness = 0; // 0 a 255
uint8_t blue_led_brightness = 0; // 0 a 255
static bool blue_increasing = true;  
static bool green_increasing = true;
static bool red_increasing = true;

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
        if (temporizador_ativo && absolute_time_diff_us(get_absolute_time(), desligar_em) <= 0) {
            red_led_brightness = green_led_brightness = blue_led_brightness = 0;
        
            // Atualiza os níveis PWM para refletir o desligamento
            uint slice_red = pwm_gpio_to_slice_num(LED_RED_PIN);
            uint channel_red = pwm_gpio_to_channel(LED_RED_PIN);
            pwm_set_chan_level(slice_red, channel_red, 0);
        
            uint slice_green = pwm_gpio_to_slice_num(LED_GREEN_PIN);
            uint channel_green = pwm_gpio_to_channel(LED_GREEN_PIN);
            pwm_set_chan_level(slice_green, channel_green, 0);
        
            uint slice_blue = pwm_gpio_to_slice_num(LED_BLUE_PIN);
            uint channel_blue = pwm_gpio_to_channel(LED_BLUE_PIN);
            pwm_set_chan_level(slice_blue, channel_blue, 0);
        
            temporizador_ativo = false;  // Desativa o temporizador
            printf("LEDs desligados automaticamente após 5 minutos.\n");
        }
        
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

// Frequências das notas musicais (em Hz)
enum NotasMusicais {
    DO = 2640, // Dó
    RE = 2970, // Ré
    MI = 3300, // Mi
    FA = 3520, // Fá
    SOL = 3960, // Sol
    LA = 4400, // Lá
    SI = 4950  // Si
};

// Configura o PWM no pino do buzzer com uma frequência especificada
void set_buzzer_frequency(uint pin, uint frequency) {
    // Obter o slice do PWM associado ao pino
    uint slice_num = pwm_gpio_to_slice_num(pin);

    // Configurar o pino como saída de PWM
    gpio_set_function(pin, GPIO_FUNC_PWM);

    // Configurar o PWM com frequência desejada
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / (frequency * 4096)); // Calcula divisor do clock

    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(pin, 0); // Inicializa com duty cycle 0 (sem som)
}

void atualizar_pwm_leds() {
    uint slice_red = pwm_gpio_to_slice_num(LED_RED_PIN);
    uint channel_red = pwm_gpio_to_channel(LED_RED_PIN);

    uint slice_green = pwm_gpio_to_slice_num(LED_GREEN_PIN);
    uint channel_green = pwm_gpio_to_channel(LED_GREEN_PIN);

    uint slice_blue = pwm_gpio_to_slice_num(LED_BLUE_PIN);
    uint channel_blue = pwm_gpio_to_channel(LED_BLUE_PIN);

    pwm_set_chan_level(slice_red, channel_red, red_led_brightness);
    pwm_set_chan_level(slice_green, channel_green, green_led_brightness);
    pwm_set_chan_level(slice_blue, channel_blue, blue_led_brightness);
}


// Função para tocar o buzzer por um tempo especificado (em milissegundos)
void play_buzzer(uint pin, uint frequency, uint duration_ms) {

    set_buzzer_frequency(pin, frequency);   
    pwm_set_gpio_level(pin, 32768);           
    sleep_ms(duration_ms);                   
    pwm_set_gpio_level(pin, 0);              
}

// Função para tocar a nota Dó
void playDo(uint duration_ms) {
    green_led_brightness = 255; // Acende LED verde
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN, DO, duration_ms);
    green_led_brightness = 0; // Apaga LED verde
    atualizar_pwm_leds();
}


// Função para tocar a nota Ré
void playRe(uint duration_ms) {
    blue_led_brightness = 255; // Acende LED azul
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN, RE, duration_ms);
    blue_led_brightness = 0; // Apaga LED azul
    atualizar_pwm_leds();
}

// Função para tocar a nota Mi
void playMi(uint duration_ms){
    red_led_brightness = 255; // Acende LED vermelho
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN,MI,duration_ms);
    red_led_brightness = 0; // Apaga LED vermelho
    atualizar_pwm_leds();
}

// Função para tocar a nota Fá
void playFa(uint duration_ms){
    green_led_brightness = 255; // Acende LED verde
    atualizar_pwm_leds();
    blue_led_brightness = 255; // Acende LED azul
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN,FA,duration_ms);
    green_led_brightness = 0; // Apaga LED verde
    atualizar_pwm_leds();
    blue_led_brightness = 0; // Apaga LED azul
    atualizar_pwm_leds();
}

// Função para tocar a nota Sol
void playSol(uint duration_ms) {
    green_led_brightness = 255; // Acende LED verde
    atualizar_pwm_leds();
    red_led_brightness = 255; // Acende LED vermelho
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN, SOL, duration_ms);
    green_led_brightness = 0; // Apaga LED verde
    atualizar_pwm_leds();
    red_led_brightness = 0; // Apaga LED vermelho
    atualizar_pwm_leds();
}

// Função para tocar a nota Lá
void playLa(uint duration_ms) {
    blue_led_brightness = 255; // Acende LED azul
    atualizar_pwm_leds();
    red_led_brightness = 255; // Acende LED vermelho
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN, LA, duration_ms);
    blue_led_brightness = 0; // Apaga LED azul
    atualizar_pwm_leds();
    red_led_brightness = 0; // Apaga LED vermelho
    atualizar_pwm_leds();
}

// Função para tocar a nota Si
void playSi(uint duration_ms) {
    green_led_brightness = 255; // Acende LED verde
    atualizar_pwm_leds();
    blue_led_brightness = 255; // Acende LED azul
    atualizar_pwm_leds();
    red_led_brightness = 255; // Acende LED vermelho
    atualizar_pwm_leds();
    play_buzzer(BUZZER_PIN, SI, duration_ms);
    green_led_brightness = 0; // Apaga LED verde
    atualizar_pwm_leds();
    blue_led_brightness = 0; // Apaga LED azul
    atualizar_pwm_leds();
    red_led_brightness = 0; // Apaga LED vermelho
    atualizar_pwm_leds();
}

void tocar_musica_festa() {
    playSol(900);
    sleep_ms(100);
    playLa(500);
    sleep_ms(100);
    playSol(900);
    sleep_ms(100);
    playMi(1500);
    sleep_ms(100);

    playSol(900);
    sleep_ms(100);
    playLa(500);
    sleep_ms(100);
    playSol(900);
    sleep_ms(100);
    playMi(1500);
    sleep_ms(100);

    playRe(900);
    sleep_ms(100);
    playRe(700);
    sleep_ms(100);
    playSi(1000);
    sleep_ms(100);

    playDo(900);
    sleep_ms(100);
    playDo(700);
    sleep_ms(100);
    playSol(1000);
    sleep_ms(100);

    playLa(900);
    sleep_ms(100);
    playLa(700);
    sleep_ms(100);
    playDo(700);
    sleep_ms(100);
    playSi(400);
    sleep_ms(100);
    playLa(300);
    sleep_ms(100);
    playSol(300);
    sleep_ms(100);
    playLa(200);
    sleep_ms(100);

    playSol(800);
    sleep_ms(100);
    playMi(1000);
    sleep_ms(100);

    playLa(800);
    sleep_ms(100);
    playLa(500);
    sleep_ms(100);
    playDo(500);
    sleep_ms(100);

    playSi(200);
    sleep_ms(100);
    playLa(400);
    sleep_ms(100);
    playSol(600);
    sleep_ms(100);

    playLa(200);
    sleep_ms(100);
    playSol(400);
    sleep_ms(100);
    playMi(1000);
    sleep_ms(100);

    playRe(700);
    sleep_ms(100);
    playRe(300);
    sleep_ms(100);
    playFa(600);
    sleep_ms(100);

    playRe(200);
    sleep_ms(100);
    playSi(400);
    sleep_ms(100);
    playDo(1000);
    sleep_ms(100);
    playMi(1000);
    sleep_ms(100);

    playDo(600);
    sleep_ms(100);
    playSol(200);
    sleep_ms(100);
    playMi(400);
    sleep_ms(100);
    playSol(600);
    sleep_ms(100);
    playFa(200);
    sleep_ms(100);
    playRe(400);
    sleep_ms(100);
    playDo(1000);
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
void user_request(char **request) {
    if (strstr(*request, "GET /blue") != NULL) {
        if (blue_increasing) {
            blue_led_brightness += 10;
            if (blue_led_brightness >= 30) {
                blue_led_brightness = 30;
                blue_increasing = false;
            }
        } else {
            if (blue_led_brightness >= 10) {
                blue_led_brightness -= 10;
            } else {
                blue_increasing = true;
            }
        }
    } else if (strstr(*request, "GET /green") != NULL) {
        if (green_increasing) {
            green_led_brightness += 10;
            if (green_led_brightness >= 30) {
                green_led_brightness = 30;
                green_increasing = false;
            }
        } else {
            if (green_led_brightness >= 10) {
                green_led_brightness -= 10;
            } else {
                green_increasing = true;
            }
        }
    } else if (strstr(*request, "GET /red") != NULL) {
        if (red_increasing) {
            red_led_brightness += 10;
            if (red_led_brightness >= 30) {
                red_led_brightness = 30;
                red_increasing = false;
            }
        } else {
            if (red_led_brightness >= 10) {
                red_led_brightness -= 10;
            } else {
                red_increasing = true;
            }
        }
    } else if (strstr(*request, "GET /alarm") != NULL)
    {
        tocar_alarme();
    } else if (strstr(*request, "GET /timer") != NULL)
    {
        desligar_em = make_timeout_time_ms(5 * 1000);  // 5 segundos
        temporizador_ativo = true;
    } else if (strstr(*request, "GET /festa") != NULL)
    {
        tocar_musica_festa();
    } else if (strstr(*request, "GET /noturno") != NULL)
    {
        blue_led_brightness = 15; // Acende LED azul
        atualizar_pwm_leds();
        green_led_brightness = 15; // Acende LED verde
        atualizar_pwm_leds();
        red_led_brightness = 15; // Acende LED vermelho
        atualizar_pwm_leds();
    } 
    
    atualizar_pwm_leds();

}

// Função para ler a temperatura
float temp_read(void) {
    adc_select_input(4);
    const float conversion_factor = 3.3f / (1 << 12);
    uint16_t raw = adc_read();
    float voltage = raw * conversion_factor;
    float temperature = 27 - (voltage - 0.706) / 0.001721;
    return temperature;
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *request = malloc(p->len + 1);
    if (!request) {
        pbuf_free(p);
        return ERR_MEM;
    }

    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Requisição recebida:\n%s\n", request);

    user_request(&request);  // Processar a requisição (altera brilho, etc.)
    atualizar_pwm_leds();

    // Gerar resposta HTML
    char html[1024];
    float temperature = temp_read(); // Leitura da temperatura

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
             "<h1>Painel de Controle</h1>\n"
             "<form action=\"./blue\"><button>Alterar Azul</button></form>\n"
             "<form action=\"./green\"><button>Alterar Verde</button></form>\n"
             "<form action=\"./red\"><button>Alterar Vermelho</button></form>\n"
             "<form action=\"./alarm\"><button>Tocar Alarme</button></form>" 
             "<form action=\"./timer\"><button>Desligar em 5 seg</button></form>"
             "<form action=\"./festa\"><button>Modo Festa</button></form>\n"
             "<form action=\"./noturno\"><button>Modo Noturno</button></form>\n"
             "<p class=\"temperature\">Temperatura Interna: %.2f &deg;C</p>\n"
             "</body>\n"
             "</html>\n",
             temperature);

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