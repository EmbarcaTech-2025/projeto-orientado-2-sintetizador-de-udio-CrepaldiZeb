# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Versão do CMake
cmake_minimum_required(VERSION 3.13)

# Importa o SDK do Raspberry Pi Pico
include(pico_sdk_import.cmake)

# Define a placa utilizada
set(PICO_BOARD pico_w)

# Define o nome do projeto
project(audio_recorder C CXX ASM)

# Define os padrões C e C++
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Inicializa o SDK
pico_sdk_init()

# Adiciona o executável e seus arquivos fonte.
add_executable(${PROJECT_NAME}
    main.c
    inc/ssd1306_i2c.c
)

# Adiciona o diretório 'inc' 
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# Habilita a saída de printf via USB e/ou UART
pico_enable_stdio_uart(${PROJECT_NAME} 1)
pico_enable_stdio_usb(${PROJECT_NAME} 1)

# Vincula as bibliotecas de hardware necessárias para o projeto
target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    hardware_adc
    hardware_pwm
    hardware_i2c
    hardware_clocks
    hardware_dma      
)

pico_add_extra_outputs(${PROJECT_NAME})