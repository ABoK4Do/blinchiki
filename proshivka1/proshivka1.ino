/*
 * Листинг 5, основной.  
 * 1. Выбираем упражнение.
 * 2. Выбираем максимальное число повторов для него.
 * 3. Считаем число повторов для упражнения "изолированное сгибание руки".
 *    или для упражнения "вертикальная (боковая) тяга".
 * 4. Фиксируем верхнюю и нижнюю точку, выводим число повторов в порт.
 *    и на мобильное приложение через BLE.
 * 5. Фиксируем ошибку в течение 3-4. Ошибку выводим голосом и светодиодом.     
 * 
 */
#include <CurieBLE.h>
#include <CurieIMU.h>
#include <SoftwareSerial.h>
#include <CurieTimerOne.h>
#include <LiquidCrystal.h>
#include <DFPlayer_Mini_Mp3.h>

/*** BLE data  ***/

//Создаем перефирийное устройство: 
BLEPeripheral blePeripheral; 
//Создаем сервис и присваиваем ему UUID: 
BLEService dumbbellService("19B10010-E8F2-537E-4F6C-D104768A1214");

 
//BLERead - запрос значение характеристики из мобильного приложения; 
//BLENotify - непрерывный опрос характеристики телефоном; 
//BLEWrite - отправка сообщения от телефона плате.

// Создание характеристики типа int, в которую будет записываться 
// текущее число повторов (rpt: гантеля -> смартфон) 
// и подтверждающий сигнал о выборе упражнения
BLEIntCharacteristic exCharacteristic
    ("19B10012-E8F2-537E-4F6C-D104768A1216", BLERead | BLEWrite | BLENotify);
BLEIntCharacteristic scoreCharacteristic
    ("19B10013-E8F2-537E-4F6C-D104768A1217", BLERead | BLEWrite | BLENotify);
BLEIntCharacteristic rptCharacteristic
    ("19B10014-E8F2-537E-4F6C-D104768A1218", BLERead | BLEWrite | BLENotify);

void initBLE (void);

/*** BLE data end  ***/

//Порт под MP3-плеер:
SoftwareSerial mySerial(0, 1); // RX (на TX у плеера), TX (на RX у плеера)
#define BUSY_PIN 3
 
//Порты:
//Свет
#define RED_PIN 9
#define GREEN_PIN 8
//Кнопки
#define BUT_ONE 2

//Константы 
#define holdtime 2500
#define holdtime2 500
#define ON LOW
#define OFF HIGH
unsigned long eventTime=0;
#define MAX_EX 2
int wait = 1;
int rpt = 1;
bool waserr = false;

//Очки
int rpt_limit = 1;
int score = 0;
int combo = 0;


//Инициирующее значение по ключевой оси. Его достижение означает 
//прохождение нижней точки в упражнении (0 - инициализиция):
//И инициирующее значение по осям контроля. Отклонение от этих значений на величину, 
//большую чем err_limit означает ошибку выполнения упражнения.
//(0.0 - просто инициализиция переменной):
float y_init = 0.0, x_init = 0.0, z_init = 0.0;


//Значение МАКСИМАЛЬНО допустимой разницы между текущим
//положением гантели и начальным y_init.
//Если значение стало меньше dy_down_limit,
//значит мы пришли в нижнюю точку.
float dy_down_limit = 0.2;

//Значение МИНИМАЛЬНО допустимой разницы между текущим
//положением гантели и начальным y_init.
//Если разница между текущим y и y_init стала 
//больше dy_up_limit_isolated_flexion (dy_up_limit_vertical_traction),
//мы пришли в верхнюю точку.
//Зачение dy_up_limit_isolated_flexion (dy_up_limit_vertical_traction) 
//калибруется в зависимости от abs(y_init).
float dy_up_limit_isolated_flexion  = 0;
float dy_up_limit_vertical_traction = 0;


//Переменная кнопки One
int ButOneChecker = 0;

void calibrate_isolated_flexion (bool voice);
void calibrate_vertical_traction (bool voice);

void ex_isolated_flexion (int);
void ex_vertical_traction (int);

bool test_error_isolated_flexion (void);
bool test_error_vertical_traction (void);

void LEDLight(char color) ; 

void ShowExScreen (int ex_number);

void ShowUpScreen(int rpt_limit);

void ShowScore(int score, int combo, int rpt);

#define ISOLATED_FLECTION 1
#define VERTICAL_TRACTION 2


// Инициализируем объект-экран, передаём использованные 
// для подключения контакты на Arduino в порядке:
// RS, E, DB4, DB5, DB6, DB7
LiquidCrystal lcd(4, 5, 10, 11, 12, 13);
byte EX_MUSIC[] = {102, 101};


void setup() {
    // устанавливаем размер (количество столбцов и строк) экрана
    lcd.begin(16, 2);
    lcd.print("Hello, friend!");
    pinMode(RED_PIN, OUTPUT);
    pinMode(GREEN_PIN, OUTPUT);
    pinMode(BUT_ONE, INPUT);
    //Мигаем светодиодом 4 раза в секунду с помощью прерывания таймера:
    //CurieTimerOne.start(oneSecInUsec / 4,  &timed_blink_isr);            
    //Последовательный порт:
    Serial.begin(9600);

    
    //Инициализируем гироскопы-акселерометры:
    CurieIMU.begin();
    CurieIMU.setGyroRate(25);
    CurieIMU.setAccelerometerRate(25);
    //Устанавливаем акселерометр на диапазон 2G:
    CurieIMU.setAccelerometerRange(2);
    //Устанавливаем гироскоп на диапазон 250 градусов:
    CurieIMU.setGyroRange(250);
    
    //Настройки MP3:
    //3 порт - на нем читаем состояние плеера (свободен-занят)
    pinMode(BUSY_PIN, INPUT); 
    mySerial.begin (9600);
    mp3_set_serial(mySerial); //Отдаем RX-TX
    delay(1);
    mp3_set_volume (25); //Звук в диапазоне 0-30
    //Ждем завершения переходных процессов, 
    //иначе music может не сработать:
    delay(300);
    
    randomSeed(analogRead(0));
    music(random(1, 4));
  
    
    
    delay(2000);
    
    //Запускаем BLE:
    initBLE();
}



void loop() {
    if(wait)
    delay(3000);
    lcd.clear();
    rpt_limit = 1;
    //Выбираем упражнение (по умолчанию - изолированное сгибание):
    int ex_number = ISOLATED_FLECTION;
    
    
    
    
    lcd.print("3a\xB6\xBC\xB8\xBF\x65 \xBA\xBDo\xBE\xBAy"); //Зажмите кнопку
    lcd.setCursor(0, 1);
    //Вывод на экран "упражнение
    lcd.print("\xE3\xBB\xC7 \xB3\xC3\xB2opa");//для выбора
    delay(1000);

    while(true){
      if(digitalRead(BUT_ONE) == ON && !eventTime) {
        lcd.clear();
        lcd.setCursor(0, 0);
        ShowExScreen(ex_number);
        music(EX_MUSIC[ex_number-1]);
         
        eventTime=millis(); // засекли когда произошло событие
        
      }
      if(digitalRead(BUT_ONE) == OFF && eventTime) {
        eventTime = 0;
        break;
      }
      if(eventTime && (millis()-eventTime>holdtime)){ // проверям прошло ли 2000 миллесекунд с события
        if(ex_number == MAX_EX) ex_number = 1;
        else ex_number++;
        lcd.clear();
        ShowExScreen(ex_number);
        eventTime = 0;
      }
      
    }
    lcd.clear();
    exCharacteristic.setValue(ex_number);
    rptCharacteristic.setValue(0);
    scoreCharacteristic.setValue(0);
    lcd.print("\x4B\x6F\xBB\x2D\xB3\x6F");//Кол-во
    lcd.setCursor(0,1);
    lcd.print("\xBE\x6F\xB3\xBF\x6F\x70\x65\xBD\xB8\xB9");//повторений
     while(true){
      if(digitalRead(BUT_ONE) == ON && !eventTime) {
        eventTime=millis(); // засекли когда произошло событие
          lcd.clear();
        lcd.setCursor(0, 0);
        ShowUpScreen(rpt_limit);
        
        
      }
      if(digitalRead(BUT_ONE) == OFF && eventTime) {
        eventTime = 0;
        break;
      }
      if(eventTime && (millis()-eventTime>holdtime2)){ // проверям прошло ли 500 миллесекунд с события
         rpt_limit++;
         lcd.clear();
         ShowUpScreen(rpt_limit);
         eventTime = 0;
      }
      
    }
    

    

    

    //Дождались номера упражнения, если он корректный, ждем число повторов:    
    if ((ex_number == ISOLATED_FLECTION) or (ex_number == VERTICAL_TRACTION)) {
        
        

        
        
       

        if (ISOLATED_FLECTION == ex_number) {
          lcd.clear();
            lcd.print("\x4B\x61\xBB\xB8\xB2\x70\x6F\xB3\xBA\x61... "); //Калибровка...
         
            delay(1500);
            
            calibrate_isolated_flexion(true);
            lcd.clear();  
            lcd.print("\xA1\x6F\xBF\x6F\xB3\x6F\x21");//Готово!
            lcd.setCursor(0,1);
            lcd.print("\x48\x61\xC0\xB8\xBD\x61\xB9\xBF\x65");//Начинайте
            music(5);
            LEDLight('G');
            delay(500);
            LEDLight('O');
            CurieTimerOne.pause();
            
            //
            ex_isolated_flexion(rpt_limit);            
        
        } else if (VERTICAL_TRACTION == ex_number) {
          lcd.clear();
            lcd.print("\x4B\x61\xBB\xB8\xB2\x70\x6F\xB3\xBA\x61... "); //Калибровка...
            
           
            delay(1500);
            
            calibrate_vertical_traction(true); 
            lcd.clear(); 
            lcd.print("\xA1\x6F\xBF\x6F\xB3\x6F\x21");//Готово!
            lcd.setCursor(0,1);
            lcd.print("\x48\x61\xC0\xB8\xBD\x61\xB9\xBF\x65");//Начинайте
            music(5);
            LEDLight('G');
            delay(500);
            LEDLight('O');
            CurieTimerOne.pause();
           
            //
            ex_vertical_traction(rpt_limit);    
        }
        
    }  else {
        
        
        lcd.print("error");
        
    }

}


//УПРАЖНЕНИЯ:

void ex_isolated_flexion (int rpt_limit) {
    
   
    
    //Текущая координата по ключевой оси:
    float y;
    //Текущая (абсолютная) разница между данными показаниями по оси 
    //и их инициирующим значением.
    //Чем она больше, тем на больший угол мы отклонились по оси:
    float dy;
    
    
    score = 5;
    for (rpt = 1; rpt <= rpt_limit; rpt++) {    
       bool error = false;
       
        //Пока движемся наверх и dy меньше предела, 
        //проверяем текущую ошибку по вспомогательным осям:
        do {
            y  = CurieIMU.readAccelerometerScaled(Y_AXIS);
            dy = abs(y - y_init);

            //Проверяем на ошибку, уведомляем, если ошиблись:
            error = test_error_isolated_flexion();  
            if (error) waserr = true;          
        } while ((dy < dy_up_limit_isolated_flexion) || error);
        
         //Говорим, "и, раз!":
         LEDLight('G');
         music(8);
         delay(200);
         LEDLight('O');
        

        //Движемся вниз:
        do {
            y  = CurieIMU.readAccelerometerScaled(Y_AXIS);
            dy = abs(y - y_init);

            //Снова проверяем на ошибку, уведомляем, если ошиблись:
            error = test_error_isolated_flexion(); 
            if (error) waserr = true;
        } while ((dy > dy_down_limit) || error);
        
        //Тут, в принципе, можно перекалибровать данные y_init
        //в нижней точке вызовом calibrate_isolated_flexion(false);
        //Говорим, "и, два!":
        LEDLight('G');
        music(9);
         delay(200);
         LEDLight('O');
         
        //Количество сделанных повторов выводим на экран:
        
        score = score+5*combo;
        if(!waserr) combo++;
        
        rptCharacteristic.setValue(rpt);
        scoreCharacteristic.setValue(score);
        ShowScore(score,combo,rpt);
        waserr = false;
        //BLE SCORE

        
    }    
   
    lcd.clear();
    lcd.print("\x42\xC3\xBE\x6F\xBB\xBD\x65\xBD\x6F");//Выполнено!
    lcd.setCursor(0,1);
    lcd.print("Score:");//Score
    lcd.print((String)score);
    music(7);
    
    score = 0;
    combo = 1;
    wait = 0;
    LEDLight('O');
    while(true){
      if(digitalRead(BUT_ONE)==ON) break;
    }
}

void ex_vertical_traction (int rpt_limit) {  
    
    
    //Текущая координата по ключевой оси:
    float y;
    //Текущая (абсолютная) разница между данными показаниями по оси 
    //и их инициирующим значением.
    //Чем она больше, тем на больший угол мы отклонились по оси:
    float dy;
    
    
    
    for (rpt = 1; rpt <= rpt_limit; rpt++) {    
       bool error = false;
       
        //Пока движемся наверх и dy меньше предела, 
        //проверяем текущую ошибку по вспомогательным осям:
        do {
            y  = CurieIMU.readAccelerometerScaled(Y_AXIS);
            dy = abs(y - y_init);

            //Проверяем на ошибку, уведомляем, если ошиблись:
            error = test_error_vertical_traction(); 
            if (error) waserr = true;           
        } while ((dy < dy_up_limit_vertical_traction) || error);
        
        //Говорим, "и, раз!":
        LEDLight('G');
        music(8);
         delay(200);
         LEDLight('O');
       

        //Движемся вниз:
        do {
            y  = CurieIMU.readAccelerometerScaled(Y_AXIS);
            dy = abs(y - y_init);

            //Снова проверяем на ошибку, уведомляем, если ошиблись:
            error = test_error_vertical_traction();
            if (error) waserr = true; 
        } while ((dy > dy_down_limit) || error);
        
        //Тут, в принципе, можно перекалибровать данные y_init
        //в нижней точке вызовом calibrate_isolated_flexion(false);
        
        //Говорим, "и, два!":
        LEDLight('G');
        music(9);
         delay(200);
         LEDLight('O');
            
        //Количество сделанных повторов выводим на экран:
        
        score = score+5*combo;
        if(!waserr) combo++;
        rptCharacteristic.setValue(rpt);
        scoreCharacteristic.setValue(score);
        ShowScore(score,combo,rpt);
        
        //BLE SCORE

        
    }    

   lcd.clear();
    lcd.print("\x42\xC3\xBE\x6F\xBB\xBD\x65\xBD\x6F");//Выполнено!
    lcd.setCursor(0,1);
    lcd.print("Score:");
    lcd.print((String)score);
    music(7);
    
    score = 0;
    combo = 1;
    wait = 0;
    LEDLight('O');
    while(true){
      if(digitalRead(BUT_ONE)==ON) break;
    }
    music(55);
    delay(500);
}


//ОШИБКИ:

/*
 *  Проверяем текущую ошибку по вспомогательным осям (здесь X).
 *  Если ошибка вышла за предел err_limit,
 *  выводим голосовое предупреждение.
 */
bool test_error_isolated_flexion (void) {
    //Максимально допустимое отклонение по вспомогательным осям. 
    //Выход за это отклонение означает ошибку выполнения упражнения:
    float err_limit = 0.45;
    
    static bool err_flag = false;    
    static unsigned long int err_time = millis();

    
    float x  = CurieIMU.readAccelerometerScaled(X_AXIS);       
    float dx = abs(x - x_init);

    const int SIZE = 20;
    static int i = -1;
    //Ошибки собираем в массив и считаем среднее, чтобы
    //защититься от ситуации пограничных флуктуаций
    //когда текущее dx находится вблизи предела err_limit
    static float error_list[SIZE];

    //Инициализация:
    if (-1 == i) {
        for (int j = 0; j < SIZE; j++) error_list[j] = 0; 
    }

    //ФИКСИРУЕМ НОВУЮ ОШИБКУ:
    i = (i+1)%SIZE;
    error_list[i] = dx;

    
    float error = 0.0;
    for (int j = 0; j < SIZE; j++) {
        error += error_list[j];
    }
    error /= SIZE;

    //Если функция ошибки вышла за предел и сделала это ТОЛЬКО ЧТО
    //(err_flag опущен; так защищаемся от "атаки ошибок"), 
    //то обновляем измерение и оповещаем спортсмена.
    if ((error > err_limit) && !err_flag && (millis() > err_time+1000)) {

        err_time = millis();
        err_flag = true;

        char buffer[255];
        sprintf(
            buffer, 
            "%.2f: err_flag ON with error = %.2f; %c", err_time/1000.0, 
            error, 0
        );
        combo = 1;
        ShowScore(score,combo,rpt);
        //Предупреждаем: "Неправильно!"
        music(11);
        LEDLight('R');
        delay(500);
           
    }    

    //Если же мы в корректной области, то сбрасываем флаг ошибки,
    //чтобы иметь возможность реагировать на новые:   
    if ((error < err_limit) && err_flag) {
        err_flag = false;  
        LEDLight('O');              
        char buffer[255];
        sprintf(buffer, "%.2f: err_flag OFF %c", millis()/1000.0, 0);
        Serial.println(buffer);
    }

    

    return err_flag;
}


bool test_error_vertical_traction (void) {
    //Максимально допустимое отклонение по вспомогательным осям. 
    //Выход за это отклонение означает ошибку выполнения упражнения:
    float err_limit = 0.33;
    
    static bool err_flag = false;    
    
    static unsigned long int err_time = millis();

    
    float x  = CurieIMU.readAccelerometerScaled(X_AXIS);       
    float dx = abs(x - x_init);

    const int SIZE = 20;
    static int i = -1;
    //Ошибки собираем в массив и считаем среднее, чтобы
    //защититься от ситуации пограничных флуктуаций
    //когда текущее dx находится вблизи предела err_limit
    static float error_list[SIZE];

    //Инициализация:
    if (-1 == i) {
        for (int j = 0; j < SIZE; j++) error_list[j] = 0; 
    }

    //ФИКСИРУЕМ НОВУЮ ОШИБКУ:
    i = (i+1)%SIZE;
    error_list[i] = dx;

    
    float error = 0.0;
    for (int j = 0; j < SIZE; j++) {
        error += error_list[j];
    }
    error /= SIZE;

    //Если функция ошибки вышла за предел и сделала это ТОЛЬКО ЧТО
    //(err_flag опущен; так защищаемся от "атаки ошибок"), 
    //то обновляем измерение и оповещаем спортсмена.
    if ((error > err_limit) && !err_flag && (millis() > err_time+1000)) {

        err_time = millis();
        err_flag = true;

        char buffer[255];
        sprintf(
            buffer, 
            "%.2f: err_flag ON with error = %.2f; %c", 
            err_time/1000.0, 
            error, 
            0
        );
        combo = 1;
        ShowScore(score,combo,rpt);
        //Предупреждаем: "Неправильно!"
        LEDLight('R');
        music(11);
        delay(500);   
    }    

    //Если же мы в корректной области, то сбрасываем флаг ошибки,
    //чтобы иметь возможность реагировать на новые:   
    if ((error < err_limit) && err_flag) {
        err_flag = false;  
        LEDLight('O');              
        char buffer[255];
        sprintf(buffer, "%.2f: err_flag OFF %c", millis()/1000.0, 0);
        Serial.println(buffer);
    }

    

    return err_flag;
}


//КАЛИБРОВКИ:

/*
 *  Калибруем начальные значения по ключевой оси.
 *  y_init - некоторое время только нули,
 *  в акселерометре идут какие-то переходные процессы.
 *  Дожидаемся, пока не придет что-то полезное 
 *  и выставляем dy_up_limit_isolated_flexion в зависимости от полученного значения.
 */
void calibrate_isolated_flexion (bool voice) {    
   
     
        //Ждем пару секунд, чтобы спортсмен успел встать 
        //в исходную позицию на калибровку:
        delay(3000);
    

    y_init = 0;
    
    //Калибруем, пока не получим значение, отличное от нуля в 
    //двух знаках после запятой (y_init*100):
    int y = y_init * 100;
    while (y == 0) {
        y_init = CurieIMU.readAccelerometerScaled(Y_AXIS);
        y = y_init * 100;
    }   
    //Наберем сотню значений за секунду и возьмем среднее:
    for (int i = 1; i < 100; i++){
        delay(10);
        y_init += CurieIMU.readAccelerometerScaled(Y_AXIS);
    }
    y_init /= 100;
    
    
    //Калибруем величину dy_up_limit_isolated_flexion в зависимости от y_init 
    //(апостериорные данные).
    //Наибольшим y_init соответствует максимальный лимит:
    if (0.8 < abs(y_init)) {
        dy_up_limit_isolated_flexion = 1.65;
    }
    
    //Постепенно понижаем лимит при уменьшении y_init:
    if (0.8 >= abs(y_init)) {
        dy_up_limit_isolated_flexion = 1.46;
    }
    
    if (0.5 >= abs(y_init)) {
        dy_up_limit_isolated_flexion = 1.1;
    }
    
    if (0.31 >= abs(y_init)) {
        dy_up_limit_isolated_flexion = 0.9;
    }
    
    //То ли это ошибка нормировки, то ли выделенная точка, 
    //но при 0.0 - предельное значение вот такое:
    if (0.0 == abs(y_init)) {
        dy_up_limit_isolated_flexion = 1.8;
    }


    //Теперь калибруем вспомогательные оси. 
    //Вопрос о калибровке предельной ошибки 
    //в зависимости от показаний - остается открытым:
    x_init = 0;
    int x = x_init * 100;
    while (x == 0) {
        x_init = CurieIMU.readAccelerometerScaled(X_AXIS);
        x = x_init * 100;
    }
    z_init = 0;
    int z = z_init * 100;
    while (z == 0) {
        z_init = CurieIMU.readAccelerometerScaled(Z_AXIS);
        z = z_init * 100;
    }    
}

void calibrate_vertical_traction (bool voice) {    
   
   
        //Ждем пару секунд, чтобы спортсмен успел встать 
        //в исходную позицию на калибровку:
        delay(3000);
    

    y_init = 0;
    
    //Калибруем, пока не получим значение, отличное от нуля в 
    //двух знаках после запятой (y_init*100):
    int y = y_init * 100;
    while (y == 0) {
        y_init = CurieIMU.readAccelerometerScaled(Y_AXIS);
        y = y_init * 100;
    }   
    //Наберем сотню значений за секунду и возьмем среднее:
    for (int i = 1; i < 100; i++){
        delay(10);
        y_init += CurieIMU.readAccelerometerScaled(Y_AXIS);
    }
    y_init /= 100;
    
    
    //Калибруем величину dy_up_limit_isolated_flexion в зависимости от y_init 
    //(апостериорные данные).
    //Наибольшим y_init соответствует максимальный лимит:
    if (0.8 < abs(y_init)) {
        dy_up_limit_vertical_traction = 1.65;
    }
    
    //Постепенно понижаем лимит при уменьшении y_init:
    if (0.8 >= abs(y_init)) {
        dy_up_limit_vertical_traction = 1.46;
    }
    
    if (0.5 >= abs(y_init)) {
        dy_up_limit_vertical_traction = 1.1;
    }
    
    if (0.31 >= abs(y_init)) {
        dy_up_limit_vertical_traction = 0.9;
    }
    
    //То ли это ошибка нормировки, то ли выделенная точка, 
    //но при 0.0 - предельное значение вот такое:
    if (0.0 == abs(y_init)) {
        dy_up_limit_vertical_traction = 1.8;
    }

    dy_up_limit_vertical_traction *=  0.85;

    //Теперь калибруем вспомогательные оси. 
    //Вопрос о калибровке предельной ошибки 
    //в зависимости от показаний - остается открытым:
    x_init = 0;
    int x = x_init * 100;
    while (x == 0) {
        x_init = CurieIMU.readAccelerometerScaled(X_AXIS);
        x = x_init * 100;
    }
    
    z_init = 0;
    int z = z_init * 100;
    while (z == 0) {
        z_init = CurieIMU.readAccelerometerScaled(Z_AXIS);
        z = z_init * 100;
    }    
}




//Вклю светодиодом с помощью прерываний таймера:
void LEDLight(char color)   
{                  
  if(color == 'R') {
      digitalWrite(RED_PIN, HIGH);
      digitalWrite(GREEN_PIN, LOW);
  }
  if(color == 'G') {
     digitalWrite(RED_PIN, LOW);
     digitalWrite(GREEN_PIN, HIGH);
  }
  if(color == 'D') {
     digitalWrite(RED_PIN, HIGH);
     digitalWrite(GREEN_PIN, HIGH);
  }
  if(color == 'O'){
     digitalWrite(RED_PIN, LOW);
     digitalWrite(GREEN_PIN, LOW);
  }
}



void initBLE (void)
{
  blePeripheral.setLocalName("Dumbbell");
  blePeripheral.setAdvertisedServiceUuid(dumbbellService.uuid());
  blePeripheral.addAttribute(dumbbellService);
  blePeripheral.addAttribute(exCharacteristic);
  blePeripheral.addAttribute(scoreCharacteristic);
  blePeripheral.addAttribute(rptCharacteristic);

  //Ожидаем подключения:
  blePeripheral.begin();
}

void ShowExScreen(int ex_number){
  lcd.clear();
  
  switch(ex_number) {
    case 1: 
      lcd.print("\xA5\xB7o\xBB\xB8po\xB3\x61\xBD\xBDoe"); //Изолированное
      lcd.setCursor(0,1);
      lcd.print("\x63\xB4\xB8\xB2\x61\xBD\xB8\x65");//сгибание
      break;
    case 2:
      lcd.print("Bep\xBF\xB8\xBA\x61\xBB\xC4\xBD\x61\xC7");//Вертикальная
      lcd.setCursor(0,1);
      lcd.print("\xBF\xC7\xB4\x61");//тяга
      break;
  }
}

void ShowUpScreen(int rpt_limit){
  lcd.setCursor(0, 0);
  lcd.print((String)rpt_limit);
}
void ShowScore(int score, int combo, int rpt) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Score:");
  lcd.print((String)score);
  lcd.setCursor(0,1);
  lcd.print("X");
  lcd.print((String)combo);
  lcd.setCursor(13,1);
  lcd.print((String)rpt);
}
/*
 * Воспроизведение заданного трека.
 *
 * Приветствия:
 * 0001  - все склонятся...
 * 0002  - да, хозяин
 * 0003  - да воцарится..
 * 0004  - пришло время бодрости
 * 
 * Упражнение:
 * 0005 - начали
 * 0006, 0007 - закончили
 * 0008 - и раз
 * 0009 - и два
 * 0010, 0011 - не халтурь
 *
 * Название упражнений:
 * 0101 - Вертикальная тяга
 * 0102 - Изолированное сгибание
 * 
 * Доп треки
 * 0055 - Опять работа?
 */
void music(int i) {
    boolean play_state;
    do {
        play_state = digitalRead(BUSY_PIN); //BUSY_PIN = 3
    } while (play_state == LOW);

    mp3_play(i);

    do {
        play_state = digitalRead(BUSY_PIN);
    } while (play_state == LOW);
}

