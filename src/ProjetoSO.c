// Joao Melo, 2019216747
// Miguel Faria, 2019216809

// Includes
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <pthread.h>
#include <time.h>

// Defines
#define MAXLINESIZE 20
#define BUF_LENGTH 256
#define PIPE_NAME "namedPipe"
#define MAX_PIPES 50
#define DEBUG

// Global variables
pid_t father_id;

FILE *fichLog;

typedef struct {
  int unidade_tempo;
  int dist_volta;
  int num_voltas;
  int num_equipas;
  int max_carros_equipa;
  int tempo_calc_avaria;
  int T_Box_min;
  int T_Box_max;
  int capacidade;
} configs;

configs configuracoes;

typedef struct {
    int n_equipa;
    int n_carro;
} paramCarro;

// Time variables
time_t t;
struct tm tm;

// Semaphores
sem_t *sem_log;
sem_t *sem_shm;

// Shared Memory
int shmid;

typedef struct {
    pthread_t car;
    char state[15];
    double gasTank;
    double totalDist;
    int number;
    double speed;
    double consumption;
    int reliability;
    int malfunction;
    int box;
    char team[BUF_LENGTH];
    int nmrBoxes;
    int nmrMalfunctions;
    int time;
} car;

typedef struct {
    char name[BUF_LENGTH];
    car *cars;
    char box_state[12];
    int nmrRefuels;
} team;

typedef struct {
    team *teams;
    int raceInterrupt;
    int raceFinished;
} shm;

shm *sharedMemory;

// Pipes
int unnamedPipeEquipa[MAX_PIPES][2];
int unnamedPipeCarro[MAX_PIPES][2];

// Threads
pthread_mutex_t mutex_carros = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_sairBox = PTHREAD_COND_INITIALIZER;

// Message Queue
int mqid;

typedef struct {
    long mtype;
    int carro;
} messageQueue;



////////////////////////////////////////////////////////////////////////////////
// Funcoes que introduzem valores em structs

void valoresConfigs(configs *configs, int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    configs->unidade_tempo = a;
    configs->dist_volta = b;
    configs->num_voltas = c;
    configs->num_equipas = d;
    configs->max_carros_equipa = e;
    configs->tempo_calc_avaria = f;
    configs->T_Box_min = g;
    configs->T_Box_max = h;
    configs->capacidade = i;
}

void valoresCar(car *car, char *b, double c, int d, double e, double f, int g, char *h) {
    strcpy(car->state, b);
    car->gasTank = c;
    car->number = d;
    car->speed = e;
    car->consumption = f;
    car->reliability = g;
    strcpy(car->team, h);
}

void valoresTeam(team *team, char *a, char *c) {
    strcpy(team->name, a);
    strcpy(team->box_state, c);
}


////////////////////////////////////////////////////////////////////////////////
// Funcoes que transformam strings

char* upper_string(char s[]) {
    int c = 0;
    while (s[c] != '\0') {
      if (s[c] >= 'a' && s[c] <= 'z') {
         s[c] = s[c] - 32;
      }
      c++;
    }
    return s;
}


int myAtoi(char s[]) {
    int erro = 0;
    int res = 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i]>='0' && s[i]<='9') {
            res = res * 10 + s[i] - '0';
        }
        else {
            erro = -1;
            break;
        }
    }

    if (erro == 0)
        return res;
    else
        return erro;
}


double myAtof(char s[]) {
    int ponto = 1;
    double erro = 0;
    double res;
    char temp[strlen(s)];
    int i;
    for (i = 0; s[i] != '\0'; i++) {
        if ((s[i]>='0' && s[i]<='9'))
            temp[i] = s[i];
        else if (s[i] == '.' && ponto == 1) {
            ponto = 0;
            temp[i] = s[i];
        }
        else {
            erro = -1;
            break;
        }
    }
    temp[i] = '\0';
    res = atof(temp);

    if (erro == 0)
        return res;
    else
        return erro;
}


////////////////////////////////////////////////////////////////////////////////
//Funcoes para escrever no ficheiro 'log.txt'

void logMessage1(char *message) {
    sem_wait(sem_log);
	t = time(NULL);
	tm = *localtime(&t);
    #ifdef DEBUG
	printf("%d:%d:%d %s\n",tm.tm_hour,tm.tm_min,tm.tm_sec,message);
    #endif
	fprintf(fichLog, "%d:%d:%d %s\n",tm.tm_hour,tm.tm_min,tm.tm_sec,message);
	fflush(fichLog);
	sem_post(sem_log);
}

void logMessage2(char *message, int i) {
	sem_wait(sem_log);
	t = time(NULL);
	tm = *localtime(&t);
    #ifdef DEBUG
	printf("%d:%d:%d %s %d' CREATED\n",tm.tm_hour,tm.tm_min,tm.tm_sec,message,i);
    #endif
	fprintf(fichLog, "%d:%d:%d %s %d' CREATED\n",tm.tm_hour,tm.tm_min,tm.tm_sec,message,i);
	fflush(fichLog);
	sem_post(sem_log);
}

void logError(char *message, char* i) {
	sem_wait(sem_log);
	t = time(NULL);
	tm = *localtime(&t);
	fprintf(fichLog, "%d:%d:%d %s %s\n",tm.tm_hour,tm.tm_min,tm.tm_sec,message,i);
	fflush(fichLog);
	sem_post(sem_log);
}


////////////////////////////////////////////////////////////////////////////////
// Funcao que finaliza o programa

void finish(int finish) {
    if (getpid() == father_id) {
        logMessage1("SIMULATOR CLOSING");

        printf("Cleaning resources...\n");

        fclose(fichLog);

        sem_close(sem_log);
        sem_unlink("SEM_LOG");
        sem_close(sem_shm);
        sem_unlink("SEM_SHM");

        for (int i=0; i<MAX_PIPES; i++) {
            close(unnamedPipeEquipa[i][0]);
            close(unnamedPipeEquipa[i][1]);
            close(unnamedPipeCarro[i][0]);
            close(unnamedPipeCarro[i][1]);
        }

        unlink(PIPE_NAME);

        for (int i=0; i<2+configuracoes.num_equipas; i++)
            wait(NULL);

        if (mqid >= 0)
            msgctl(mqid, IPC_RMID, NULL);

        if (shmid >= 0)
            shmctl(shmid, IPC_RMID, NULL);

        printf("Closing program!\n");
    }
    exit(finish);
}


////////////////////////////////////////////////////////////////////////////////
// Funcao responsavel pela estatistica

int converteTemp(struct tm tempo){
    int total = 0;
    total += tempo.tm_hour * 24;
    total += tempo.tm_min * 60;
    total += tempo.tm_sec;

    return total;
}


void estatisticas(){
    car top5carros[5], lastPlace;
    int total = 0, totalAvarias = 0,totalAbastecimentos = 0;

    for(int i = 0; i < configuracoes.num_equipas;i++){
        totalAbastecimentos += sharedMemory->teams[i].nmrRefuels;
        for(int j=0;j<configuracoes.max_carros_equipa && strlen(sharedMemory->teams[i].cars[j].state)!=0;j++){

            totalAvarias += sharedMemory->teams[i].cars[j].nmrMalfunctions;
            if(strcmp(sharedMemory->teams[i].cars[j].state,"TERMINADO")!=0 && strcmp(sharedMemory->teams[i].cars[j].state,"DESISTENCIA")!=0)
                total++;
            if(i==0 && j==0)
                lastPlace = sharedMemory->teams[i].cars[j];

            else if(lastPlace.totalDist>sharedMemory->teams[i].cars[j].totalDist ||
            (strcmp(sharedMemory->teams[i].cars[j].state,"TERMINADO")==0
            && strcmp(lastPlace.state,sharedMemory->teams[i].cars[j].state)==0 &&
            lastPlace.time < sharedMemory->teams[i].cars[j].time))
                lastPlace = sharedMemory->teams[i].cars[j];

            if(strcmp(sharedMemory->teams[i].cars[j].state,"TERMINADO")==0){
                for(int p = 0; p<5;p++){
                    if(strlen(top5carros[p].state) == 0){
                        top5carros[p] = sharedMemory->teams[i].cars[j];
                        p = 5;
                    }
                    else if((strcmp(top5carros[p].state,sharedMemory->teams[i].cars[j].state)==0 && top5carros[p].time > sharedMemory->teams[i].cars[j].time)
                        || strcmp(top5carros[p].state,sharedMemory->teams[i].cars[j].state)!=0){
                        car temp = top5carros[p];
                        top5carros[p] = sharedMemory->teams[i].cars[j];
                        for(p=p+1; p <5; p++){
                            if(strlen(top5carros[p].state) != 0){
                                car temp1 = top5carros[p];
                                top5carros[p] = temp;
                                temp = temp1;
                            }
                            else{
                                top5carros[p] = temp;
                                p = 5;
                            }
                        }
                    }
                }
            }
            else{
                for(int p = 0; p<5;p++){
                    if(strlen(top5carros[p].state) == 0) {
                        top5carros[p] = sharedMemory->teams[i].cars[j];
                        p = 5;
                    }
                    else if(top5carros[p].totalDist < sharedMemory->teams[i].cars[j].totalDist){
                        car temp = top5carros[p];
                        top5carros[p] = sharedMemory->teams[i].cars[j];
                        for(p=p+1; p <5; p++){
                            if(strlen(top5carros[p].state) != 0){
                                car temp1 = top5carros[p];
                                top5carros[p] = temp;
                                temp = temp1;
                            }
                            else{
                                top5carros[p] = temp;
                                p = 5;
                            }
                        }
                    }
                }
            }
        }
    }
    printf("\n############## STATISTICS ##############\n# TOP 5\n");
    for(int i = 0;i<5;i++){
        printf("     %dº => CAR %d, TEAM %s - %d COMPLETED LAPS, %d PITSTOPS\n",i+1,top5carros[i].number,top5carros[i].team,(int) top5carros[i].totalDist/configuracoes.dist_volta,top5carros[i].nmrBoxes);
    }
    printf("# LASTPLACE\n     CAR %d, TEAM %s - %d COMPLETED LAPS, %d PITSTOPS\n",lastPlace.number,lastPlace.team,(int) lastPlace.totalDist/configuracoes.dist_volta,lastPlace.nmrBoxes);
    printf("# TOTAL MALFUNCTIONS\n     %d MALFUNCTIONS\n",totalAvarias);
    printf("# TOTAL REFUELS\n     %d REFUELS\n",totalAbastecimentos);
    printf("# TOTAL CARS ON TRACK\n     %d CARS\n########################################\n\n",total);

    for (int i=0; i<5; i++) {
        memset(top5carros, 0, sizeof(top5carros));
    }
}


////////////////////////////////////////////////////////////////////////////////
// Funcoes que faz tratamento de sinais

void signals(int s) {
    if ((sharedMemory->raceFinished == -1 && s == SIGINT) || getpid() == father_id) {
        if (s == SIGINT)  {

            if (getpid() == father_id) {
                printf("\n");
                logMessage1("Signal SIGINT received");
                if (sharedMemory->raceFinished != -1) {
                    sem_wait(sem_shm);
                    sharedMemory->raceInterrupt = -1;
                    sem_post(sem_shm);

                    while(sharedMemory->raceFinished != 1);
                }
            }
            finish(0);
        }
        else if(s == SIGTSTP){
            printf("\n");
            logMessage1("Signal SIGTSTP received");
            estatisticas();
        }
    }
    if(s == SIGUSR1){
        if(sharedMemory->raceInterrupt != -1){
            printf("\n");
            logMessage1("Signal SIGUSR1 received");
            sem_wait(sem_shm);
            sharedMemory->raceInterrupt = 1;
            sem_post(sem_shm);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////
// Funcao que le o ficheiro 'FicheiroDeConfiguracao.txt'

configs lerFicheiroDeConfiguracao() {
    int dataConfig[9];
    FILE *fichConfig;
    fichConfig = fopen("FicheiroDeConfiguracao.txt", "r");
    if (fichConfig == NULL) {
        perror("Error opening file 'FicheiroDeConfiguracao.txt'");
        logError("Error opening file 'FicheiroDeConfiguracao.txt':", strerror(errno));
        finish(1);
    }
    int a = 0;
    char linha[MAXLINESIZE];
    while (fgets(linha, MAXLINESIZE, fichConfig) != NULL) {
        int x = strlen(linha) -2;
        if (linha[x] == '\n')
            linha[x]='\0';
        char *elem = strtok(linha, ", ");
        while(elem != NULL) {
            dataConfig[a++] = atoi(elem);
            elem = strtok(NULL, ", ");
        }
    }
    fclose(fichConfig);

    configs configs_aux = {};
    if (a != 9 || dataConfig[3]<3) {
        perror("Incorrect or incomplete file 'FicheiroDeConfiguracao.txt'");
        logError("Incorrect or incomplete file 'FicheiroDeConfiguracao.txt':", strerror(errno));
        finish(1);
        return configs_aux;
    }
    else {
        valoresConfigs(&configs_aux, dataConfig[0], dataConfig[1], dataConfig[2],
            dataConfig[3], dataConfig[4], dataConfig[5], dataConfig[6], dataConfig[7], dataConfig[8]);
        return configs_aux;
    }
}


////////////////////////////////////////////////////////////////////////////////
// Funcao que le comandos do named pipe

void lerComandos(int fd_np) {
    while (sharedMemory->raceInterrupt != -1) {
        sem_wait(sem_shm);
        sharedMemory->raceFinished=-1;
        sharedMemory->raceInterrupt=0;
        sem_post(sem_shm);

        int nread;
        char comando[BUF_LENGTH];
        char temp[BUF_LENGTH];
        char *caracteristicas[11];
        int i, j, a, z;

        do {
            nread = read(fd_np, comando, BUF_LENGTH);
            if (nread > 0) {
                comando[nread-1] = '\0';
                strcpy(temp, comando);
                if (strcmp(upper_string(temp), "START RACE!") == 0) {
                    for (i=0; i<configuracoes.num_equipas && strlen(sharedMemory->teams[i].name)!=0; i++) {}
                    if (i == configuracoes.num_equipas) {
                        char message[BUF_LENGTH + 50];
                        sprintf(message, "NEW COMMAND RECEIVED: %s", comando);
                        logMessage1(message);
                        for (j=0; j<configuracoes.num_equipas; j++) {
                            write(unnamedPipeEquipa[j][1], temp, strlen(temp));
                        }
                    }
                    else {
                        char message[BUF_LENGTH + 50];
                        sprintf(message, "%s => CANNOT START, NOT ENOUGH TEAMS", comando);
                        logMessage1(message);
                        memset(temp, 0, sizeof(temp));
                    }
                }

                else {
                    a = 0;
                    char aux[strlen(temp)];
                    strcpy(aux, temp);
                    char *elem = strtok(aux, ", ");
                    while(elem != NULL) {
                        caracteristicas[a++] = elem;
                        elem = strtok(NULL, ", ");
                    }

                    z=0;
                    for (i=0; i<11; i++) {
                        if (caracteristicas[i] == NULL) {
                            z=1;
                            break;
                        }
                    }

                    j=0;
                    if (z==0 && strcmp(caracteristicas[0], "ADDCAR") == 0 && strcmp(caracteristicas[1], "TEAM:") == 0
                        && strcmp(caracteristicas[3], "CAR:") == 0 && myAtoi(caracteristicas[4]) != -1
                        && strcmp(caracteristicas[5], "SPEED:") == 0 && myAtoi(caracteristicas[6]) != -1
                        && strcmp(caracteristicas[7], "CONSUMPTION:") == 0 && myAtof(caracteristicas[8]) != -1
                        && strcmp(caracteristicas[9], "RELIABILITY:") == 0 && myAtoi(caracteristicas[10]) != -1) {
                        for (i=0; i<configuracoes.num_equipas && strlen(sharedMemory->teams[i].name)!=0; i++) {
                            if (strcmp(caracteristicas[2],sharedMemory->teams[i].name) == 0) {
                                for (j=0; j<configuracoes.max_carros_equipa && strlen(sharedMemory->teams[i].cars[j].state)!=0; j++) {
                                    if (myAtoi(caracteristicas[4]) == sharedMemory->teams[i].cars[j].number) {
                                        logMessage1("CAR ALREADY EXISTS");
                                        i = configuracoes.num_equipas + 1;
                                        break;
                                    }
                                }
                                if (i < configuracoes.num_equipas && j != configuracoes.max_carros_equipa) {
                                    sem_wait(sem_shm);
                                    valoresCar(&sharedMemory->teams[i].cars[j], "BOX", configuracoes.capacidade,
                                        myAtoi(caracteristicas[4]), myAtoi(caracteristicas[6]), myAtof(caracteristicas[8]),
                                        myAtoi(caracteristicas[10]), caracteristicas[2]);
                                    sem_post(sem_shm);
                                    i = configuracoes.num_equipas + 1;

                                    char message[BUF_LENGTH + 50];
                                    sprintf(message, "NEW CAR LOADED => %s", comando);
                                    logMessage1(message);
                                }
                                else {
                                    if (j == configuracoes.max_carros_equipa)
                                        logMessage1("TEAM ALREADY FULL");
                                    break;
                                }
                            }
                        }
                        if (i == configuracoes.num_equipas)
                            logMessage1("THERE ARE NO SLOTS AVAILABLE FOR ANY MORE TEAMS");

                        else if (i<configuracoes.num_equipas && j != configuracoes.max_carros_equipa) {
                            sem_wait(sem_shm);
                            valoresTeam(&sharedMemory->teams[i], caracteristicas[2], "OCUPADA");
                            sem_post(sem_shm);

                            sem_wait(sem_shm);
                            valoresCar(&sharedMemory->teams[i].cars[j], "BOX", configuracoes.capacidade,
                                myAtoi(caracteristicas[4]), myAtoi(caracteristicas[6]), myAtof(caracteristicas[8]),
                                myAtoi(caracteristicas[10]), caracteristicas[2]);
                            sem_post(sem_shm);

                            char message[BUF_LENGTH + 50];
                            sprintf(message, "NEW CAR LOADED => %s", comando);
                            logMessage1(message);
                        }
                    }
                    else {
                        char message[BUF_LENGTH + 50];
                        sprintf(message, "WRONG COMMAND => %s", comando);
                        logMessage1(message);
                    }

                    for (i=0; i<11; i++)
                        memset(caracteristicas, 0, sizeof(caracteristicas));
                }
            }
            memset(comando, 0, sizeof(comando));
        } while (strcmp(upper_string(temp), "START RACE!") && sharedMemory->raceInterrupt == 0);

        memset(temp,0,sizeof(temp));
        sem_wait(sem_shm);
        sharedMemory->raceFinished = 0;
        sem_post(sem_shm);
        signal(SIGUSR1, signals);

        int carrosPresentes = 0;
        for (i=0; i<configuracoes.num_equipas; i++) {
            for (j=0; j<configuracoes.max_carros_equipa && strlen(sharedMemory->teams[i].cars[j].state)!=0; j++) {
                carrosPresentes++;
            }
        }

        int carrosTerminados = 0,enviou = 0,primeiro = 0;
        do {
            nread = read(fd_np, comando, BUF_LENGTH);
            char message[BUF_LENGTH + 50];

            if (nread > 0) {
                comando[nread-1] = '\0';
                sprintf(message, "%s => REJECTED, RACE ALREADY STARTED", comando);
                logMessage1(message);
                memset(comando, 0, sizeof(comando));
            }

            fd_set readCarros;
            FD_ZERO(&readCarros);
            for (i=0; i<configuracoes.num_equipas; i++) {
                FD_SET(unnamedPipeCarro[i][0], &readCarros);
            }
            if (select(unnamedPipeCarro[configuracoes.num_equipas-1][0]+1, &readCarros, NULL, NULL, NULL) > 0) {
                for (i=0; i<configuracoes.num_equipas; i++) {
                    if (FD_ISSET(unnamedPipeCarro[i][0], &readCarros)) {

                        nread = read(unnamedPipeCarro[i][0], &message,  BUF_LENGTH);
                        if (nread > 0) {
                            message[nread] = '\0';
                            int a = 0;
                            char *p[2];
                            char *elem2 = strtok(message, " ");
                            while(elem2 != NULL) {
                                p[a++] = elem2;
                                //printf("%s\n", p[a-1]);
                                elem2 = strtok(NULL, " ");
                            }
                            if (strcmp(p[0],"BOX") == 0) {
                                sprintf(message, "CAR %d IS IN THE BOX", myAtoi(p[1]));
                                logMessage1(message);
                            }
                            else if(strcmp(p[0],"CORRIDA") == 0){
                                sprintf(message, "CAR %d ENTERED RACE MODE", myAtoi(p[1]));
                                logMessage1(message);
                            }
                            else if(strcmp(p[0],"SEGURANCA") == 0){
                                sprintf(message, "CAR %d ENTERED SECURITY MODE", myAtoi(p[1]));
                                logMessage1(message);
                            }

                            if (strcmp(p[0],"TERMINADO") == 0 || strcmp(p[0],"DESISTENCIA") == 0) {
                                if(strcmp(p[0],"TERMINADO") == 0 && primeiro == 0){
                                    sprintf(message, "CAR %d WINS THE RACE", myAtoi(p[1]));
                                    primeiro++;
                                }
                                else if(strcmp(p[0],"TERMINADO") == 0)
                                    sprintf(message, "CAR %d FINISHED THE RACE", myAtoi(p[1]));
                                else if(strcmp(p[0],"DESISTENCIA") == 0)
                                    sprintf(message, "CAR %d GAVE UP", myAtoi(p[1]));
                                logMessage1(message);
                                carrosTerminados++;
                            }

                            for (int j=0; j<2; j++)
                                memset(p, 0, sizeof(p));
                        }
                    }
                }
            }


            for (i=0; i<configuracoes.num_equipas; i++) {
                if(sharedMemory->raceInterrupt != 0 && enviou < configuracoes.num_equipas){
                    write(unnamedPipeEquipa[i][1], "RACE INTERRUPTED", sizeof("RACE INTERRUPTED"));
                    enviou++;
                }
                else if ( carrosTerminados == carrosPresentes) {
                    sem_wait(sem_shm);
                    sharedMemory->raceFinished = 1;
                    sem_post(sem_shm);
                    if(sharedMemory->raceInterrupt == 0)
                        write(unnamedPipeEquipa[i][1], "RACE FINISHED", sizeof("RACE FINISHED"));
                }

                else if (sharedMemory->raceInterrupt == 0)
                    write(unnamedPipeEquipa[i][1], "CONTINUE", sizeof("CONTINUE"));

            }
        } while (carrosTerminados != carrosPresentes);
        if(sharedMemory->raceInterrupt == 1){
            estatisticas();
            logMessage1("YOU CAN NOW RESTART THE RACE BY EXECUTING 'START RACE!' COMMAND");}
        fflush(stdin);

    }
    close(fd_np);

}


////////////////////////////////////////////////////////////////////////////////
// Processos e Threads

void gestorAvarias() {
    logMessage1("'GESTOR DE AVARIAS' CREATED");
    messageQueue msg;

    int avaria;

    int i, j;
    int flag = 0;
    while(sharedMemory->raceInterrupt != -1){
        do {

            sleep(3);
            for (i=0; i<configuracoes.num_equipas && flag!=1; i++) {
                for (j=0; j<configuracoes.max_carros_equipa && flag!=1; j++) {
                    if (strlen(sharedMemory->teams[i].cars[j].state) != 0 && strcmp(sharedMemory->teams[i].cars[j].state, "CORRIDA") == 0) {
                        flag = 1;

                    }
                }
            }

        } while (flag != 1 && sharedMemory->raceInterrupt == 0);
        if(sharedMemory->raceInterrupt == 0){
            do {
                sleep(configuracoes.tempo_calc_avaria);

                flag = 0;
                for (i=0; i<configuracoes.num_equipas && flag!=1; i++) {
                    for (j=0; j<configuracoes.max_carros_equipa && flag!=1; j++) {
                        if (strlen(sharedMemory->teams[i].cars[j].state) != 0 && (strcmp(sharedMemory->teams[i].cars[j].state, "CORRIDA") == 0
                            || strcmp(sharedMemory->teams[i].cars[j].state, "SEGURANCA") == 0 || strcmp(sharedMemory->teams[i].cars[j].state, "BOX") == 0)) {
                                flag = 1;
                        }
                    }
                }

                if (flag != 0) {
                    for (i=0; i<configuracoes.num_equipas; i++) {
                        for (j=0; j<configuracoes.max_carros_equipa; j++) {
                            car carro = sharedMemory->teams[i].cars[j];
                            avaria = rand()%100;
                            if (strlen(carro.state) != 0 && strcmp(carro.state, "TERMINADO") != 0 && strcmp(carro.state, "DESISTENCIA") != 0) {
                                msg.mtype = i*configuracoes.max_carros_equipa + j + 1;
                                if (carro.reliability < avaria) {
                                    //printf("Carro %d avariou\n", carro.number);
                                    msg.carro = carro.number;
                                    msgsnd(mqid, &msg, sizeof(msg)-sizeof(long), 0);
                                }
                                else {
                                    msg.carro = -1;
                                    msgsnd(mqid, &msg, sizeof(msg)-sizeof(long), 0);
                                }
                            }
                        }
                    }
                }
            } while (flag != 0);
        }
    }
}

// Funcao que simula as condicoes da corrida
void corrida(team *equipa, car *carro, int n_equipa) {
    char mensagem[BUF_LENGTH];

    double tempo = (double)1/(double)(configuracoes.unidade_tempo);
    int volta = (int) carro->totalDist/configuracoes.dist_volta;

    if (strcmp(carro->state,"SEGURANCA") == 0) {
        if (carro->gasTank-(tempo * carro->consumption * 0.4) > 0) {


            sem_wait(sem_shm);
            carro->totalDist += tempo * carro->speed * 0.3;
            carro->gasTank -= tempo * carro->consumption * 0.4;
            sem_post(sem_shm);
            volta = (int) carro->totalDist/configuracoes.dist_volta;
            if (carro->number == 20)

            if((carro->totalDist/configuracoes.dist_volta-volta+(tempo*carro->speed*0.3/configuracoes.dist_volta) >= 1)
                && (carro->gasTank-(configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed*0.3)*carro->consumption*0.4 > 0)) {

                sleep((configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed*0.3));
                sem_wait(sem_shm);
                carro->gasTank -= (configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed*0.3)*carro->consumption*0.4;
                carro->totalDist += configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta));
                sem_post(sem_shm);
                if (carro->totalDist >= configuracoes.num_voltas*configuracoes.dist_volta) {
                    sem_wait(sem_shm);
                    strcpy(carro->state, "TERMINADO");
                    sem_post(sem_shm);
                    sprintf(mensagem, "TERMINADO %d", carro->number);
                    write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                }
                else if (strcmp(equipa->box_state,"LIVRE") == 0 || strcmp(equipa->box_state,"RESERVADO") == 0) {

                    sem_wait(sem_shm);
                    strcpy(carro->state, "BOX");
                    carro->box = 1;
                    carro->nmrBoxes++;
                    sem_post(sem_shm);
                    sprintf(mensagem, "BOX %d", carro->number);
                    write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);

                    pthread_mutex_lock(&mutex_carros);
                    while (carro->box == 1) {
                        pthread_cond_wait(&cond_sairBox, &mutex_carros);
                    }
                    pthread_mutex_unlock(&mutex_carros);
                    if (carro->box == 0) {
                        strcpy(carro->state, "CORRIDA");
                        sprintf(mensagem, "CORRIDA %d", carro->number);
                        write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                    }
                }
            }
        }
        else {
            double dist = (carro->gasTank/carro->consumption * 0.4)*carro->speed * 0.3;
            sem_wait(sem_shm);
            carro->totalDist += dist;
            carro->gasTank = 0;
            sem_post(sem_shm);

            if (carro->totalDist >= configuracoes.num_voltas*configuracoes.dist_volta) {
                sem_wait(sem_shm);
                strcpy(carro->state, "TERMINADO");
                sem_post(sem_shm);
                sprintf(mensagem, "TERMINADO %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
            }

            else {
                sem_wait(sem_shm);
                strcpy(carro->state, "DESISTENCIA");
                sem_post(sem_shm);
                sprintf(mensagem, "DESISTENCIA %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
            }
        }
    }

    else if (strcmp(carro->state,"CORRIDA") == 0) {
        sem_wait(sem_shm);
        carro->totalDist += tempo * carro->speed;
        carro->gasTank -= tempo * carro->consumption;
        sem_post(sem_shm);
        volta = (int) carro->totalDist/configuracoes.dist_volta;
        if(carro->totalDist >= configuracoes.num_voltas*configuracoes.dist_volta){
            sem_wait(sem_shm);
            strcpy(carro->state,"TERMINADO");
            sem_post(sem_shm);
            sprintf(mensagem, "TERMINADO %d", carro->number);
            write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
        }
        else if (carro->gasTank <= (2*configuracoes.dist_volta/carro->speed)*carro->consumption){
            sem_wait(sem_shm);
            strcpy(carro->state, "SEGURANCA");
            sem_post(sem_shm);

            if ((carro->totalDist/configuracoes.dist_volta-volta+(tempo*carro->speed*0.3/configuracoes.dist_volta) >= 1)
                && (carro->gasTank-((configuracoes.dist_volta*(1-(carro->totalDist-volta)))/(carro->speed*0.3)*carro->consumption*0.4) > 0)) {
                sleep((configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed*0.3));
                sem_wait(sem_shm);
                carro->gasTank -= (configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed*0.3)*carro->consumption*0.4;
                carro->totalDist += configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta));
                sem_post(sem_shm);
                if (carro->totalDist >= configuracoes.num_voltas*configuracoes.dist_volta) {
                    sem_wait(sem_shm);
                    strcpy(carro->state, "TERMINADO");
                    sem_post(sem_shm);
                    sprintf(mensagem, "TERMINADO %d", carro->number);
                    write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                }
                else if (strcmp(equipa->box_state,"LIVRE") == 0 || strcmp(equipa->box_state,"RESERVADO") == 0) {
                    sem_wait(sem_shm);
                    strcpy(carro->state, "BOX");
                    carro->box = 1;
                    carro->nmrBoxes++;
                    sem_post(sem_shm);
                    sprintf(mensagem, "BOX %d", carro->number);
                    write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);


                    pthread_mutex_lock(&mutex_carros);
                    while (carro->box == 1) {
                        pthread_cond_wait(&cond_sairBox, &mutex_carros);
                    }
                    pthread_mutex_unlock(&mutex_carros);
                    if (carro->box == 0) {
                        strcpy(carro->state, "CORRIDA");
                        sprintf(mensagem, "CORRIDA %d", carro->number);
                        write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                    }
                }
            }
            if (strlen(mensagem) == 0) {
                sprintf(mensagem, "CORRIDA %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
            }
        }
        else if (carro->gasTank <= (4*configuracoes.dist_volta/carro->speed)*carro->consumption
            && carro->totalDist/configuracoes.dist_volta-volta+(tempo*carro->speed/configuracoes.dist_volta) >= 1
            && strcmp(equipa->box_state,"LIVRE") == 0) {
            sleep((configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/carro->speed);
            sem_wait(sem_shm);
            carro->gasTank -= (configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/carro->speed*carro->consumption;
            carro->totalDist += configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta));
            sem_post(sem_shm);
            if (strcmp(equipa->box_state,"LIVRE") == 0) {
                sem_wait(sem_shm);
                strcpy(carro->state, "BOX");
                carro->box = 1;
                carro->nmrBoxes++;
                sem_post(sem_shm);
                sprintf(mensagem, "BOX %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);

                pthread_mutex_lock(&mutex_carros);
                while (carro->box == 1) {
                    pthread_cond_wait(&cond_sairBox, &mutex_carros);
                }
                pthread_mutex_unlock(&mutex_carros);
                if (carro->box == 0) {
                    strcpy(carro->state, "CORRIDA");
                    sprintf(mensagem, "CORRIDA %d", carro->number);
                    write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                }
            }
        }
    }
    if (strlen(mensagem) == 0)
        write(unnamedPipeCarro[n_equipa][1], "CONTINUE", BUF_LENGTH);
    memset(mensagem, 0, sizeof(mensagem));
}

// Funcao que termina a volta em caso de interrupcao da corrida
void terminaVolta(team *equipa, car *carro, int n_equipa) {
    char mensagem[BUF_LENGTH];
    double tempo = (double)1/(double)(configuracoes.unidade_tempo);
    int volta = (int) carro->totalDist/configuracoes.dist_volta;
    if ((int) carro->totalDist%configuracoes.dist_volta == 0) {
        sem_wait(sem_shm);
        strcpy(carro->state, "TERMINADO");
        sem_post(sem_shm);
        sprintf(mensagem, "TERMINADO %d", carro->number);
        write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
        memset(mensagem, 0, sizeof(mensagem));
    }
    if (strcmp(carro->state, "CORRIDA") == 0) {
        sem_wait(sem_shm);
        carro->totalDist += tempo*carro->speed;
        carro->gasTank -= tempo*carro->consumption;
        sem_post(sem_shm);
        volta = (int) carro->totalDist / configuracoes.dist_volta;
        if (carro->totalDist / configuracoes.dist_volta - volta + (tempo * carro->speed / configuracoes.dist_volta) >=1) {
            sleep((configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta - volta)))/carro->speed);
            sem_wait(sem_shm);
            carro->gasTank -= (configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/carro->speed * carro->consumption;
            carro->totalDist += configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta));
            strcpy(carro->state, "TERMINADO");
            sem_post(sem_shm);
            sprintf(mensagem, "TERMINADO %d", carro->number);
            write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
            memset(mensagem, 0, sizeof(mensagem));
        }
    }
    else if (strcmp(carro->state, "SEGURANCA") == 0) {
        if (carro->gasTank -(tempo*carro->consumption*0.4) > 0) {
            sem_wait(sem_shm);
            carro->totalDist += tempo*carro->speed*0.3;
            carro->gasTank -= tempo*carro->consumption*0.4;
            sem_post(sem_shm);
            volta = (int) carro->totalDist / configuracoes.dist_volta;
            if ((carro->totalDist/configuracoes.dist_volta-volta+(tempo*carro->speed*0.3/configuracoes.dist_volta) >= 1)
                && (carro->gasTank-(configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed*0.3)*carro->consumption*0.4 > 0)) {
                sleep((configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed * 0.3));
                sem_wait(sem_shm);
                carro->gasTank -= (configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta)))/(carro->speed * 0.3) * carro->consumption * 0.4;
                carro->totalDist += configuracoes.dist_volta*(1-(carro->totalDist/configuracoes.dist_volta-volta));
                strcpy(carro->state, "TERMINADO");
                sem_post(sem_shm);
                sprintf(mensagem, "TERMINADO %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                memset(mensagem, 0, sizeof(mensagem));
            }
        } else {
            double dist = (carro->gasTank/carro->consumption*0.4)*carro->speed*0.3;
            sem_wait(sem_shm);
            carro->totalDist += dist;
            carro->gasTank = 0;
            sem_post(sem_shm);

            if ((int)carro->totalDist%configuracoes.dist_volta==0) {
                sem_wait(sem_shm);
                strcpy(carro->state, "TERMINADO");
                sem_post(sem_shm);
                sprintf(mensagem, "TERMINADO %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                memset(mensagem, 0, sizeof(mensagem));
            } else {
                sem_wait(sem_shm);
                strcpy(carro->state, "DESISTENCIA");
                sem_post(sem_shm);
                sprintf(mensagem, "DESISTENCIA %d", carro->number);
                write(unnamedPipeCarro[n_equipa][1], mensagem, BUF_LENGTH);
                memset(mensagem, 0, sizeof(mensagem));
            }
        }
    }
}


// Funcao usada nas threads carro
void *funcaoCarros(void* x) {
    paramCarro n = *((paramCarro *) x);
    team *equipa = &sharedMemory->teams[n.n_equipa];
    car *carro = &sharedMemory->teams[n.n_equipa].cars[n.n_carro];


    logMessage2("'CAR", carro->number);

    char mensagem[BUF_LENGTH];
    sem_wait(sem_shm);
    strcpy(carro->state, "CORRIDA");
    sprintf(mensagem, "CORRIDA %d", carro->number);
    write(unnamedPipeCarro[n.n_equipa][1], mensagem, BUF_LENGTH);
    memset(mensagem, 0, sizeof(mensagem));
    carro->malfunction = 0;
    carro->totalDist = 0;
    carro->nmrBoxes = 0;
    carro->nmrMalfunctions = 0;
    sem_post(sem_shm);

    time_t tempo1 = time(NULL);
    struct tm startRaceTime = *localtime(&tempo1);

    messageQueue msg;
    msg.carro = -1;
    while(strcmp(carro->state, "TERMINADO") != 0 && strcmp(carro->state, "DESISTENCIA") != 0) {
        double tempo = (double)1/(double)(configuracoes.unidade_tempo);
        sleep(tempo);
        if (sharedMemory->raceInterrupt != 0)
            terminaVolta(equipa, carro, n.n_equipa);
        else
            corrida(equipa, carro, n.n_equipa);

        msgrcv(mqid, &msg, sizeof(msg)-sizeof(long), n.n_equipa*configuracoes.max_carros_equipa+n.n_carro+1, IPC_NOWAIT);
        if (msg.carro == carro->number && carro->malfunction != 1
            && strcmp(carro->state, "TERMINADO") != 0 && strcmp(carro->state, "DESISTENCIA") != 0) {

            sem_wait(sem_shm);
            carro->malfunction = 1;
            if (strcmp(carro->state, "CORRIDA") == 0) {
                strcpy(carro->state, "SEGURANCA");
                sprintf(mensagem, "SEGURANCA %d", carro->number);
                write(unnamedPipeCarro[n.n_equipa][1], mensagem, BUF_LENGTH);
                sprintf(mensagem, "CAR %d DETECTED NEW MALFUNCTION", carro->number);
                logMessage1(mensagem);
                memset(mensagem, 0, sizeof(mensagem));
                carro->nmrMalfunctions++;

            }
            sem_post(sem_shm);
        }
    }

    time_t tempo2 = time(NULL);
    struct tm tempFinal = *localtime(&tempo2);
    if(tempFinal.tm_hour - startRaceTime.tm_hour < 0)
        tempFinal.tm_hour += 24;
    if(tempFinal.tm_min - startRaceTime.tm_min < 0)
        tempFinal.tm_min += 60;
    if(tempFinal.tm_sec - startRaceTime.tm_sec < 0)
        tempFinal.tm_sec += 60;
    sem_wait(sem_shm);
    tempFinal.tm_hour = tempFinal.tm_hour - startRaceTime.tm_hour;
    tempFinal.tm_min = tempFinal.tm_min - startRaceTime.tm_min;
    tempFinal.tm_sec = tempFinal.tm_sec - startRaceTime.tm_sec;
    carro->time = converteTemp(tempFinal);
    sem_post(sem_shm);


    pthread_exit(NULL);
}

void gestorEquipas(int n_equipa) {
    logMessage2("'EQUIPA", n_equipa);

    team *equipa = &sharedMemory->teams[n_equipa];
    while(sharedMemory->raceInterrupt != -1){

        sleep(3);
        char message[BUF_LENGTH];
        int nread;
        do {
            nread = read(unnamedPipeEquipa[n_equipa][0], message, sizeof(message));
            message[nread] = '\0';
        } while (strcmp(message, "START RACE!") != 0);

        sem_wait(sem_shm);
        strcpy(equipa->box_state, "LIVRE");
        equipa->nmrRefuels = 0;

        sem_post(sem_shm);

        int i;
        int nmr_carros_equipa = 0;
        for (i=0; i<configuracoes.max_carros_equipa && strlen(equipa->cars[i].state)!=0; i++) {
            nmr_carros_equipa++;
        }
        paramCarro pCarro[nmr_carros_equipa];
        for (i=0; i<nmr_carros_equipa; i++) {
            pCarro[i].n_equipa = n_equipa;
            pCarro[i].n_carro = i;
            pthread_create(&equipa->cars[i].car, NULL, funcaoCarros, &pCarro[i]);
        }

        do {
            nread = read(unnamedPipeEquipa[n_equipa][0], message, sizeof(message));
            message[nread] = '\0';

            if (strcmp(message, "RACE FINISHED") != 0 && strcmp(message, "RACE INTERRUPTED") != 0) {

                for (i=0; i<configuracoes.max_carros_equipa && strlen(equipa->cars[i].state)!=0; i++) {
                    if (strcmp(equipa->cars[i].state, "SEGURANCA") == 0) {
                        if (strcmp(equipa->box_state, "LIVRE") == 0) {
                            sem_wait(sem_shm);
                            strcpy(equipa->box_state, "RESERVADO");
                            sem_post(sem_shm);

                        }
                    }
                    if (equipa->cars[i].box == 1) {
                        sem_wait(sem_shm);
                        strcpy(equipa->box_state, "OCUPADO");
                        sem_post(sem_shm);

                        if (equipa->cars[i].malfunction == 1) {
                            int tempoReparacao = (rand() %(configuracoes.T_Box_max - configuracoes.T_Box_min + 1)) + configuracoes.T_Box_min;
                            sleep(tempoReparacao);
                            sem_wait(sem_shm);
                            equipa->cars[i].malfunction = 0;
                            sem_post(sem_shm);
                        }
                        if (equipa->cars[i].gasTank <= (4*configuracoes.dist_volta/equipa->cars[i].speed)*equipa->cars[i].consumption) {
                            sleep(2*((double)1/(double)configuracoes.unidade_tempo));
                            sem_wait(sem_shm);
                            equipa->nmrRefuels++;
                            equipa->cars[i].gasTank = configuracoes.capacidade;
                            sem_post(sem_shm);
                        }

                        sem_wait(sem_shm);
                        equipa->cars[i].box = 0;
                        sem_post(sem_shm);
                        pthread_cond_signal(&cond_sairBox);
                        sem_wait(sem_shm);
                        strcpy(equipa->box_state, "LIVRE");
                        sem_post(sem_shm);
                    }
                }
            }

        } while (strcmp(message, "RACE FINISHED") != 0 && strcmp(message, "RACE INTERRUPTED") != 0);

        while(sharedMemory->raceFinished !=1);
        for (i=0; i<configuracoes.max_carros_equipa && strlen(equipa->cars[i].state)!=0; i++) {
            pthread_join(equipa->cars[i].car, NULL);
        }
    }
}

void gestorCorrida() {
    logMessage1("'GESTOR DE CORRIDA' CREATED");

    int i;

    for (i=0; i<configuracoes.num_equipas; i++) {
        pipe(unnamedPipeEquipa[i]);
    }

    for (i=0; i<configuracoes.num_equipas; i++) {
        pipe(unnamedPipeCarro[i]);
    }

    pid_t child_3;
    // Child 3 code
    for (i=0; i<configuracoes.num_equipas; i++) {
        child_3 = fork();
        if (child_3 == 0) {
            signal(SIGINT, signals);
            gestorEquipas(i);
            exit(0);
        }
    }

    int fd_np;
    if ((fd_np = open(PIPE_NAME, O_RDONLY)) < 0){
        perror("Cannot open pipe for reading");
        logError("Cannot open pipe for reading:", strerror(errno));
        finish(1);
    }

    lerComandos(fd_np);

    for(i=0;i<configuracoes.num_equipas;i++){
		wait(NULL);
	}
}

void simuladorCorrida() {
    father_id = getpid();

    configuracoes = lerFicheiroDeConfiguracao();

    // Mapeia memoria partilhada no processo
    if ((sharedMemory = (shm*) shmat(shmid, NULL, 0)) == (shm*) -1) {
        perror("Error in shmat");
        logError("Error in shmat:", strerror(errno));
        finish(1);
    }

    sem_wait(sem_shm);
    sharedMemory->raceInterrupt = 0;
    sharedMemory->raceFinished = -1;
    sem_post(sem_shm);

    sharedMemory->teams = (team*)(sharedMemory+1);
    for (int i=0; i<configuracoes.num_equipas; i++) {
        sharedMemory->teams[i].cars = (car*)(sharedMemory->teams + configuracoes.num_equipas + i*configuracoes.max_carros_equipa);
    }

    if (configuracoes.unidade_tempo >= 0) {
        pid_t child_1, child_2;
        child_1 = fork();
        if (child_1 == 0) {
            // Child 1 code
            signal(SIGINT, signals);
            gestorCorrida();
            exit(0);
        }
        else {
            // Child 2 code
            child_2 = fork();
            if (child_2 == 0) {
                signal(SIGINT, signals);
                gestorAvarias();
                exit(0);
            }
        }

        if (mkfifo(PIPE_NAME, O_CREAT|O_EXCL|0777)<0){
            perror("Cannot create pipe");
            logError("Cannot create pipe:", strerror(errno));
            finish(1);
        }

        signal(SIGINT, signals);
        signal(SIGTSTP, signals);
        while(sharedMemory->raceInterrupt != -1);
    }

    for(int i=0;i<2;i++){
		wait(NULL);
	}
}


////////////////////////////////////////////////////////////////////////////////
// Main

int main() {
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);

    fichLog = fopen("log.txt", "w");

    // Criar o semaforo sem_log
    sem_unlink("SEM_LOG");
    sem_log = sem_open("SEM_LOG", O_CREAT|O_EXCL, 0777, 1);
	if(sem_log == SEM_FAILED) {
		perror("Failure opening the semaphore SEM_LOG");
        logError("Failure opening the semaphore SEM_LOG:", strerror(errno));
        finish(1);
	}

    logMessage1("SIMULATOR STARTING");

    // Criar o semaforo sem_shm
    sem_unlink("SEM_SHM");
    sem_shm = sem_open("SEM_SHM", O_CREAT|O_EXCL, 0777, 1);
	if(sem_shm == SEM_FAILED) {
		perror("Failure opening the semaphore SEM_SHM");
        logError("Failure opening the semaphore SEM_SHM:", strerror(errno));
        finish(1);
	}

    // Criar a message queue
    if ((mqid = msgget(IPC_PRIVATE, IPC_CREAT|0777)) < 0) {
		perror("Failure opening the message queue");
        logError("Failure opening the message queue:", strerror(errno));
        finish(1);
	}

    // Criar o segmento de memoria partilhada
    if ((shmid = shmget(IPC_PRIVATE, sizeof(shm)+configuracoes.num_equipas*sizeof(team)+configuracoes.num_equipas*configuracoes.max_carros_equipa*sizeof(car), IPC_CREAT|0777)) < 0) {
        perror("Error in shmget");
        logError("Error in shmget:", strerror(errno));
        finish(1);
    }

    simuladorCorrida();

    finish(0);
}
