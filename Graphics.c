/* main.c
 * Elijah Silguero
 * date
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <ti/devices/msp/msp.h>
#include "../inc/ST7735.h"
#include "../inc/SPI.h"
#include "../inc/Clock.h"
#include "../inc/Timer.h"
#include "../inc/LaunchPad.h"
#include "maps.h"
#include "images.h"

#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 128
#define centerX SCREEN_WIDTH/2
#define centerY SCREEN_HEIGHT/2

#define MATRIX_DARK_GREEN   0x0320  // Dark green, subtle glow
#define MATRIX_GREEN        0x07E0  // Standard bright green
#define MATRIX_NEON_GREEN   0x07F0  // Intense neon green
#define MATRIX_LIME_GREEN   0x07E6  // Almost white-green
#define MATRIX_EMERALD      0x03C0  // Deep emerald green
#define MATRIX_SOFT_GREEN   0x05A0  // Muted soft green
#define MATRIX_GLOW_GREEN   0x06E0  // Slight glow effect
#define MATRIX_HACKER_GREEN 0x04E0  // Classic "hacker" terminal green

const uint16_t wallColors[] = {
    MATRIX_DARK_GREEN,
    MATRIX_GREEN,
    MATRIX_NEON_GREEN,
    MATRIX_LIME_GREEN,
    MATRIX_EMERALD,
    MATRIX_SOFT_GREEN,
    MATRIX_GLOW_GREEN,
    MATRIX_HACKER_GREEN
};


uint8_t worldMap[MAP_WIDTH][MAP_HEIGHT];
uint16_t miniMap[MAP_WIDTH * MAP_HEIGHT] = {0};

void FillMap(const uint8_t map[MAP_WIDTH][MAP_HEIGHT]) {
    for (int i = MAP_WIDTH - 1; i >= 0; i--) {
        for (int j = 0; j < MAP_HEIGHT; j++) {
            worldMap[i][j] = map[i][j];
            int index = (MAP_WIDTH - 1 - i) * MAP_HEIGHT + j;
            miniMap[index] = (map[i][j] != 0) ? ST7735_WHITE : 0;
        }
    }
}


// Player state
double posX = 22, posY = 12;  //x and y start position
double dirX = -1, dirY = 0; //initial direction vector
double planeX = 0, planeY = 0.66; //the 2d raycaster version of camera plane
int playerHealth = 50;

uint32_t time = 0; //time of current frame
uint32_t oldTime = 0; //time of previous frame

// Screen resolution
#define RESOLUTION 2
int lastDrawStart[SCREEN_WIDTH/RESOLUTION] = {0};
int lastDrawEnd[SCREEN_WIDTH/RESOLUTION] = {0};
int lastDrawHeight[SCREEN_WIDTH/RESOLUTION] = {0};
uint16_t lastColor[SCREEN_WIDTH/RESOLUTION] = {0};

void CastRays(void) {
    int index = 0;
    for (int x = 0; x < SCREEN_WIDTH; x+= RESOLUTION){
        // Calculate ray position and direction
        double cameraX = 2 * x / (double)SCREEN_WIDTH - 1; // x-coordinate in camera space
        double rayDirX = dirX + planeX * cameraX;
        double rayDirY = dirY + planeY * cameraX;

        // Which box of the map we're in
        int mapX = (int)posX;
        int mapY = (int)posY;

        // Length of ray from current position to next x or y-side
        double sideDistX;
        double sideDistY;

        // Length of ray from one x or y-side to next x or y-side
        double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1 / rayDirX);
        double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1 / rayDirY);
        double perpWallDist;

        // What direction to step in x or y direction
        int stepX;
        int stepY;

        int hit = 0; // Was there a wall hit?
        int side; // Was a NS or EW wall hit?

        // Calculate step and initial sideDist
        if (rayDirX < 0){
            stepX = -1;
            sideDistX = (posX - mapX) * deltaDistX;
        }
        else {
            stepX = 1;
            sideDistX = (mapX + 1.0 - posX) * deltaDistX;
        }
        if (rayDirY < 0){
            stepY = -1;
            sideDistY = (posY - mapY) * deltaDistY;
        }
        else{
            stepY = 1;
            sideDistY = (mapY + 1.0 - posY) * deltaDistY;
        }

        // Perform DDA
        while (hit == 0){
            // Jump to next map square in x or y direction
            if (sideDistX < sideDistY){
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            }
            else{
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            // Check if ray hit a wall;
            if (worldMap[mapX][mapY] > 0) hit = 1;
        }

        // Calculate distance from wall to camera plane
        if (side == 0) perpWallDist = (sideDistX - deltaDistX);
        else           perpWallDist = (sideDistY - deltaDistY);

        // Calculate height of line to draw on screen
        int lineHeight = (int)SCREEN_HEIGHT/perpWallDist;

        // Calculate lowest and highest pixel to fill in current stripe
        int drawStart = -lineHeight/2 + SCREEN_HEIGHT/2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + SCREEN_HEIGHT/2;
        if(drawEnd > SCREEN_HEIGHT)drawEnd = SCREEN_HEIGHT;

        // Choose wall color
        uint16_t color = wallColors[worldMap[mapX][mapY] % 8];

        // Give sides x and y different brightness
        if (side == 1) color/=2;

        // Draw pixels of the stripe as a vertical line
        if (color == lastColor[index]){ // If we're seeing the same wall
            // Only add to edge if it needs to be taller
            if (lineHeight > lastDrawHeight[index]){
                // Extend from the top
                ST7735_FillRect(x, drawStart, RESOLUTION, lastDrawStart[index]-drawStart, color);
                //Extend from the bottom
                ST7735_FillRect(x, lastDrawEnd[index], RESOLUTION, drawEnd-lastDrawEnd[index], color);
            }
            // Only remove from edge if it needs to be shorter
            else if (lineHeight < lastDrawHeight[index]){
                // Erase from top
                ST7735_FillRect(x, lastDrawStart[index], RESOLUTION, drawStart-lastDrawStart[index], ST7735_BLACK);
                //Erase from bottom
                ST7735_FillRect(x, drawEnd, RESOLUTION, lastDrawEnd[index]-drawEnd+1, ST7735_BLACK);
            }
        }
        else { // If it's a different wall we need to redraw completely
            // Erase only the column
            ST7735_FillRect(x, lastDrawStart[index], RESOLUTION, lastDrawHeight[index], ST7735_BLACK);
            // Draw only the changed part of the column
            ST7735_FillRect(x, drawStart, RESOLUTION, lineHeight, color);
        }
        // Store the new values for the next frame
        lastDrawStart[index] = drawStart;
        lastDrawEnd[index] = drawEnd;
        lastDrawHeight[index] = lineHeight;
        lastColor[index] = color;
        index++;
    }
}

void DrawCrosshair() {
    int size = 6; // Half-length of crosshair arms
    int thickness = 2; // Thickness of lines

    uint16_t color = ST7735_BLACK;

    // Draw upper vertical lines (above center)
    for (int i = -thickness / 2; i <= thickness / 2; i++) {
        ST7735_DrawFastVLine(centerX + i, centerY - size, size - 1, color);
    }

    // Draw lower vertical lines (below center)
    for (int i = -thickness / 2; i <= thickness / 2; i++) {
        ST7735_DrawFastVLine(centerX + i, centerY + 2, size - 1, color);
    }

    // Draw left horizontal lines
    for (int i = -thickness / 2; i <= thickness / 2; i++) {
        ST7735_DrawFastHLine(centerX - size, centerY + i, size - 1, color);
    }

    // Draw right horizontal lines
    for (int i = -thickness / 2; i <= thickness / 2; i++) {
        ST7735_DrawFastHLine(centerX + 2, centerY + i, size - 1, color);
    }
}

int lastHealth = 0;
void DrawHealthBar(){
    int16_t barWidth = 50, barHeight = 10;
    if (playerHealth < lastHealth){
        ST7735_FillRect(156-barWidth + playerHealth, 6, lastHealth-playerHealth, barHeight, ST7735_BLACK);
    }

    uint16_t healthColor;
    if (playerHealth >= 30){
        healthColor = MATRIX_DARK_GREEN;
    }
    else if (playerHealth >= 15){
        healthColor = ST7735_YELLOW;
    }
    else{
        healthColor = ST7735_RED;
    }
    ST7735_FillRect(156-barWidth, 6, playerHealth, barHeight, healthColor);
    lastHealth = playerHealth;
}

void RenderHUD(){
    DrawCrosshair();
    // Draw minimap
    ST7735_DrawBitmap(0, MAP_HEIGHT, miniMap, MAP_WIDTH, MAP_HEIGHT);
    // Draw position on minimap
    ST7735_DrawPixel((int16_t)posY, (int16_t)posX, MATRIX_GREEN);
    DrawHealthBar();
}


double fastSin(double x) {
    return x - (x * x * x) / 6.0;
}

double fastCos(double x) {
    return 1.0f - (x * x) / 2.0;
}

void MovePlayer(uint8_t input, double moveSpeed, double rotSpeed) {
    // Rotate left or right
    if (input & 1){
        double cosRot = fastCos(rotSpeed);
        double sinRot = -fastSin(rotSpeed);

        // Both camera direction and camera plane must be rotated
        double oldDirX = dirX;
        dirX = dirX * cosRot - dirY * sinRot;
        dirY = oldDirX * sinRot + dirY * cosRot;

        // Camera plane must be perpendicular to camera direction
        planeX = dirY;
        planeY = -dirX;
    }

    if (input & (1<<2)){
        double cosRot = fastCos(rotSpeed);
        double sinRot = fastSin(rotSpeed);

        // Both camera direction and camera plane must be rotated
        double oldDirX = dirX;
        dirX = dirX * cosRot - dirY * sinRot;
        dirY = oldDirX * sinRot + dirY * cosRot;

        // Camera plane must be perpendicular to camera direction
        planeX = dirY;
        planeY = -dirX;
    }
    
    // Move forward if no wall in front of you
    if (input & (1<<1)){
        if(worldMap[(int)(posX + 2* dirX * moveSpeed)][(int)posY] == 0) posX += dirX * moveSpeed;
        if(worldMap[(int)posX][(int)(posY + 2*dirY * moveSpeed)] == 0) posY += dirY * moveSpeed;
    }
    // Move backward if no wall behind you
    if (input & (1<<3)) {
        if(worldMap[(int)(posX - 2* dirX * moveSpeed)][(int)posY] == 0) posX -= dirX * moveSpeed;
        if(worldMap[(int)posX][(int)(posY - 2*dirY * moveSpeed)] == 0) posY -= dirY * moveSpeed;
    }
}

void SystemInit(void) {
    Clock_Init80MHz(0);
    LaunchPad_Init();
    SPI_Init();
    ST7735_InitPrintf();
    ST7735_SetRotation(1);
    TimerG12_Init();
    FillMap(OGMap); // Pick map here

    // Initialize buttons
    IOMUX->SECCFG.PINCM[PB0INDEX] = 0x00040081; // regular GPIO input, right key
    IOMUX->SECCFG.PINCM[PB1INDEX] = 0x00040081; // regular GPIO input, up key
    IOMUX->SECCFG.PINCM[PB2INDEX] = 0x00040081; // regular GPIO input, left key
    IOMUX->SECCFG.PINCM[PB3INDEX] = 0x00040081; // regular GPIO input, down key
}

uint8_t ReadKeys(){
    return (GPIOB->DIN31_0 & 0x0F);
}

uint8_t buff[8];

int main(void) {
    SystemInit();
    uint8_t buffIndex = 0;
    while(1) {
        CastRays();
        RenderHUD();

        // Simulate health loss
        if (!(rand() % 10)){
            playerHealth--;
        }
        if (!(rand() % 20)){
            playerHealth++;
        }

        oldTime = time;
        time = TIMG12->COUNTERREGS.CTR;
        double frameTime = (oldTime - time) * (12.5e-9); // Time this frame has taken, in seconds
        double fps = 1/frameTime;

        // Speed modifiers
        double moveSpeed = frameTime * 5.0; // squares/sec
        double rotSpeed = frameTime * 3.0; // rads/sec

        // Add to buffer
        buff[buffIndex] = (uint8_t)fps;
        buffIndex = (buffIndex + 1) & 0x07;

        MovePlayer(ReadKeys(), moveSpeed, rotSpeed);

        if (GPIOA->DIN31_0 & (1<<18) || !playerHealth){
            ST7735_FillScreen(0);
            printf("Game Over!\n");
            printf("(You died lol)");
            __asm volatile("bkpt; \n"); // breakpoint here
        };
    }
}