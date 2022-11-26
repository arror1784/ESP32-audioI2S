/*
 * Audio.cpp
 *
 *  Created on: Oct 26,2018
 *  Updated on: May 20,2022
 *      Author: Wolle (schreibfaul1)
 *
 */
#include "Audio.h"
#include "mp3_decoder/mp3_decoder.h"

#ifndef AUDIO_NO_SD_FS
#ifdef SDFATFS_USED
fs::SDFATFS SD_SDFAT;
#endif
#endif // AUDIO_NO_SD_FS

/* wrapper for common pattern:
 *  if(audio_info) {
 *    sprintf(chbuf, "xxx")
 *    audio_info(chbuf);
 *  } */
#define AUDIO_INFO(cmd) if(audio_info) { cmd; audio_info(chbuf); }

//---------------------------------------------------------------------------------------------------------------------
AudioBuffer::AudioBuffer(size_t maxBlockSize) {
    // if maxBlockSize isn't set use defaultspace (1600 bytes) is enough for aac and mp3 player
    if(maxBlockSize) m_resBuffSizeRAM  = maxBlockSize;
    if(maxBlockSize) m_maxBlockSize = maxBlockSize;
}

AudioBuffer::~AudioBuffer() {
    if(m_buffer)
        free(m_buffer);
    m_buffer = NULL;
}

void AudioBuffer::setBufsize(int ram, int psram) {
    if (ram > -1) // -1 == default / no change
        m_buffSizeRAM = ram;
    if (psram > -1)
        m_buffSizePSRAM = psram;
}

size_t AudioBuffer::init() {
    if(m_buffer) free(m_buffer);
    m_buffer = NULL;
    if(psramInit() && m_buffSizePSRAM > 0) {
        // PSRAM found, AudioBuffer will be allocated in PSRAM
        m_f_psram = true;
        m_buffSize = m_buffSizePSRAM;
        m_buffer = (uint8_t*) ps_calloc(m_buffSize, sizeof(uint8_t));
        m_buffSize = m_buffSizePSRAM - m_resBuffSizePSRAM;
    }
    if(m_buffer == NULL) {
        // PSRAM not found, not configured or not enough available
        m_f_psram = false;
        m_buffSize = m_buffSizeRAM;
        m_buffer = (uint8_t*) calloc(m_buffSize, sizeof(uint8_t));
        m_buffSize = m_buffSizeRAM - m_resBuffSizeRAM;
    }
    if(!m_buffer)
        return 0;
    m_f_init = true;
    resetBuffer();
    return m_buffSize;
}

void AudioBuffer::changeMaxBlockSize(uint16_t mbs){
    m_maxBlockSize = mbs;
    return;
}

uint16_t AudioBuffer::getMaxBlockSize(){
    return m_maxBlockSize;
}

size_t AudioBuffer::freeSpace() {
    if(m_readPtr >= m_writePtr) {
        m_freeSpace = (m_readPtr - m_writePtr);
    } else {
        m_freeSpace = (m_endPtr - m_writePtr) + (m_readPtr - m_buffer);
    }
    if(m_f_start)
        m_freeSpace = m_buffSize;
    return m_freeSpace - 1;
}

size_t AudioBuffer::writeSpace() {
    if(m_readPtr >= m_writePtr) {
        m_writeSpace = (m_readPtr - m_writePtr - 1); // readPtr must not be overtaken
    } else {
        if(getReadPos() == 0)
            m_writeSpace = (m_endPtr - m_writePtr - 1);
        else
            m_writeSpace = (m_endPtr - m_writePtr);
    }
    if(m_f_start)
        m_writeSpace = m_buffSize - 1;
    return m_writeSpace;
}

size_t AudioBuffer::bufferFilled() {
    if(m_writePtr >= m_readPtr) {
        m_dataLength = (m_writePtr - m_readPtr);
    } else {
        m_dataLength = (m_endPtr - m_readPtr) + (m_writePtr - m_buffer);
    }
    return m_dataLength;
}

void AudioBuffer::bytesWritten(size_t bw) {
    m_writePtr += bw;
    if(m_writePtr == m_endPtr) {
        m_writePtr = m_buffer;
    }
    if(bw && m_f_start)
        m_f_start = false;
}

void AudioBuffer::bytesWasRead(size_t br) {
    m_readPtr += br;
    if(m_readPtr >= m_endPtr) {
        size_t tmp = m_readPtr - m_endPtr;
        m_readPtr = m_buffer + tmp;
    }
}

uint8_t* AudioBuffer::getWritePtr() {
    return m_writePtr;
}

uint8_t* AudioBuffer::getReadPtr() {
    size_t len = m_endPtr - m_readPtr;
    if(len < m_maxBlockSize) { // be sure the last frame is completed
        memcpy(m_endPtr, m_buffer, m_maxBlockSize - len);  // cpy from m_buffer to m_endPtr with len
    }
return m_readPtr;
}

void AudioBuffer::resetBuffer() {
    m_writePtr = m_buffer;
    m_readPtr = m_buffer;
    m_endPtr = m_buffer + m_buffSize;
    m_f_start = true;
    // memset(m_buffer, 0, m_buffSize); //Clear Inputbuffer
}

uint32_t AudioBuffer::getWritePos() {
    return m_writePtr - m_buffer;
}

uint32_t AudioBuffer::getReadPos() {
    return m_readPtr - m_buffer;
}
//---------------------------------------------------------------------------------------------------------------------
Audio::Audio(bool internalDAC /* = false */, uint8_t channelEnabled /* = I2S_DAC_CHANNEL_BOTH_EN */, uint8_t i2sPort) {

    //    build-in-DAC works only with ESP32 (ESP32-S3 has no build-in-DAC)
    //    build-in-DAC last working Arduino Version: 2.0.0-RC2
    //    possible values for channelEnabled are:
    //    I2S_DAC_CHANNEL_DISABLE  = 0,     Disable I2S built-in DAC signals
    //    I2S_DAC_CHANNEL_RIGHT_EN = 1,     Enable I2S built-in DAC right channel, maps to DAC channel 1 on GPIO25
    //    I2S_DAC_CHANNEL_LEFT_EN  = 2,     Enable I2S built-in DAC left  channel, maps to DAC channel 2 on GPIO26
    //    I2S_DAC_CHANNEL_BOTH_EN  = 0x3,   Enable both of the I2S built-in DAC channels.
    //    I2S_DAC_CHANNEL_MAX      = 0x4,   I2S built-in DAC mode max index

    clientsecure.setInsecure();  // if that can't be resolved update to ESP32 Arduino version 1.0.5-rc05 or higher
    m_f_channelEnabled = channelEnabled;
    m_f_internalDAC = internalDAC;
    //i2s configuration
    m_i2s_num = i2sPort; // i2s port number
    m_i2s_config.sample_rate          = 16000;
    m_i2s_config.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    m_i2s_config.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    m_i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1; // high interrupt priority
    m_i2s_config.dma_buf_count        = 8;      // max buffers
    m_i2s_config.dma_buf_len          = 1024;   // max value
    m_i2s_config.use_apll             = APLL_DISABLE; // must be disabled in V2.0.1-RC1
    m_i2s_config.tx_desc_auto_clear   = true;   // new in V1.0.1
    m_i2s_config.fixed_mclk           = I2S_PIN_NO_CHANGE;


    if (internalDAC)  {

        #ifdef CONFIG_IDF_TARGET_ESP32

            log_i("internal DAC");
            m_i2s_config.mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN );

            #if ESP_ARDUINO_VERSION_MAJOR >= 2
                m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S); // vers >= 2.0.0
            #else
                m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S_MSB);
            #endif

            i2s_driver_install((i2s_port_t)m_i2s_num, &m_i2s_config, 0, NULL);
            i2s_set_dac_mode((i2s_dac_mode_t)m_f_channelEnabled);
            if(m_f_channelEnabled != I2S_DAC_CHANNEL_BOTH_EN) {
                m_f_forceMono = true;
            }

        #endif

    }
    else {
        m_i2s_config.mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);

        #if ESP_ARDUINO_VERSION_MAJOR >= 2
            m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S); // Arduino vers. > 2.0.0
        #else
            m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
        #endif

        i2s_driver_install((i2s_port_t)m_i2s_num, &m_i2s_config, 0, NULL);
        m_f_forceMono = false;
    }

    i2s_zero_dma_buffer((i2s_port_t) m_i2s_num);

    for(int i = 0; i <3; i++) {
        m_filter[i].a0  = 1;
        m_filter[i].a1  = 0;
        m_filter[i].a2  = 0;
        m_filter[i].b1  = 0;
        m_filter[i].b2  = 0;
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setBufsize(int rambuf_sz, int psrambuf_sz) {
    if(InBuff.isInitialized()) {
        log_e("Audio::setBufsize must not be called after audio is initialized");
        return;
    }
    InBuff.setBufsize(rambuf_sz, psrambuf_sz);
};

void Audio::initInBuff() {
    if(!InBuff.isInitialized()) {
        size_t size = InBuff.init();
        if (size > 0) {
            AUDIO_INFO(sprintf(chbuf, "PSRAM %sfound, inputBufferSize: %u bytes", InBuff.havePSRAM()?"":"not ", size - 1);)
        }
    }
    changeMaxBlockSize(1600); // default size mp3 or aac
}

//---------------------------------------------------------------------------------------------------------------------
esp_err_t Audio::I2Sstart(uint8_t i2s_num) {
    // It is not necessary to call this function after i2s_driver_install() (it is started automatically),
    // however it is necessary to call it after i2s_stop()
    return i2s_start((i2s_port_t) i2s_num);
}

esp_err_t Audio::I2Sstop(uint8_t i2s_num) {
    return i2s_stop((i2s_port_t) i2s_num);
}
//---------------------------------------------------------------------------------------------------------------------
esp_err_t Audio::i2s_mclk_pin_select(const uint8_t pin) {
    if(pin != 0 && pin != 1 && pin != 3) {
        log_e("Only support GPIO0/GPIO1/GPIO3, gpio_num:%d", pin);
        return ESP_ERR_INVALID_ARG;
    }

    #ifdef CONFIG_IDF_TARGET_ESP32
        switch(pin){
            case 0:
                PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
                WRITE_PERI_REG(PIN_CTRL, 0xFFF0);
                break;
            case 1:
                PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD_CLK_OUT3);
                WRITE_PERI_REG(PIN_CTRL, 0xF0F0);
                break;
            case 3:
                PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_U0RXD_CLK_OUT2);
                WRITE_PERI_REG(PIN_CTRL, 0xFF00);
                break;
            default:
                break;
        }
    #endif

    return ESP_OK;
}
//---------------------------------------------------------------------------------------------------------------------
Audio::~Audio() {
    //I2Sstop(m_i2s_num);
    //InBuff.~AudioBuffer(); #215 the AudioBuffer is automatically destroyed by the destructor
    setDefaults();
    if(m_playlistBuff) {free(m_playlistBuff); m_playlistBuff = NULL;}
    i2s_driver_uninstall((i2s_port_t)m_i2s_num); // #215 free I2S buffer
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setDefaults() {
    stopSong();
    initInBuff(); // initialize InputBuffer if not already done
    InBuff.resetBuffer();
    MP3Decoder_FreeBuffers();
    if(!m_f_m3u8data) if(m_playlistBuff) {free(m_playlistBuff); m_playlistBuff = NULL;} // free if not m3u8
    client.stop();
    client.flush(); // release memory
    clientsecure.stop();
    clientsecure.flush();
    _client = static_cast<WiFiClient*>(&clientsecure); /* default to *something* so that no NULL deref can happen */
    playI2Sremains();

    AUDIO_INFO(sprintf(chbuf, "buffers freed, free Heap: %u bytes", ESP.getFreeHeap());)

    m_f_chunked = false;                                    // Assume not chunked
    m_f_ctseen = false;                                     // Contents type not seen yet
    m_f_firstmetabyte = false;
    m_f_localfile = false;                                  // SPIFFS or SD? (onnecttoFS)
    m_f_playing = false;
    m_f_ssl = false;
    m_f_swm = true;                                         // Assume no metaint (stream without metadata)
    m_f_webfile = false;                                    // Assume radiostream (connecttohost)
    m_f_webstream = false;
    m_f_tts = false;
    m_f_firstCall = true;                                   // InitSequence for processWebstream and processLokalFile
    m_f_running = false;
    m_f_loop = false;                                       // Set if audio file should loop
    m_f_unsync = false;                                     // set within ID3 tag but not used
    m_f_exthdr = false;                                     // ID3 extended header
    m_f_rtsp = false;                                       // RTSP (m3u8)stream
    m_f_m3u8data = false;                                   // set again in processM3U8entries() if necessary
    m_f_Log = true;                                         // logging always allowed

    m_codec = CODEC_NONE;
    m_playlistFormat = FORMAT_NONE;
    m_datamode = AUDIO_NONE;
    m_audioCurrentTime = 0;                                 // Reset playtimer
    m_audioFileDuration = 0;
    m_audioDataStart = 0;
    m_audioDataSize = 0;
    m_avr_bitrate = 0;                                      // the same as m_bitrate if CBR, median if VBR
    m_bitRate = 0;                                          // Bitrate still unknown
    m_bytesNotDecoded = 0;                                  // counts all not decodable bytes
    m_chunkcount = 0;                                       // for chunked streams
    m_contentlength = 0;                                    // If Content-Length is known, count it
    m_curSample = 0;
    m_metaint = 0;                                          // No metaint yet
    m_LFcount = 0;                                          // For end of header detection
    m_controlCounter = 0;                                   // Status within readID3data() and readWaveHeader()
    m_channels = 2;                                         // assume stereo #209
    m_streamTitleHash = 0;
    m_streamUrlHash = 0;

    //TEST loop
    m_file_size = 0;
    //TEST loop
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setConnectionTimeout(uint16_t timeout_ms, uint16_t timeout_ms_ssl){
    if(timeout_ms)     m_timeout_ms     = timeout_ms;
    if(timeout_ms_ssl) m_timeout_ms_ssl = timeout_ms_ssl;
}

//---------------------------------------------------------------------------------------------------------------------
bool Audio::connecttohost(const char* host, const char* user, const char* pwd) {
    // user and pwd for authentification only, can be empty

    char* l_host = NULL; // local copy of host
    char* h_host = NULL; // pointer of l_host without http:// or https://


    if(strlen(host) == 0) {
        if(audio_info) audio_info("Hostaddress is empty");
        return false;
    }

    if(strlen(host) >= 512 - 10) {
        if(audio_info) audio_info("Hostaddress is too long");
        return false;
    }

    l_host = (char*)malloc(strlen(host) + 10);
    strcpy(l_host, host);
    trim(l_host);
    int p = indexOf(l_host, "http", 0);
    if(p > 0){
        if(audio_info) audio_info("Hostaddress is wrong");
        if(l_host) { free(l_host); l_host = NULL;}
        return false;
    }
    if(p < 0){ // http not found, shift right +7, then insert http://
        for(int i = strlen(l_host) + 1; i >= 0; i--){
            l_host[i + 7] = l_host[i];
        }
        memcpy(l_host, "http://", 7);
    }

    setDefaults();

    AUDIO_INFO(sprintf(chbuf, "Connect to new host: \"%s\"", l_host);)

    // authentification
    uint8_t auth = strlen(user) + strlen(pwd);
    char toEncode[auth + 4];
    toEncode[0] = '\0';
    strcat(toEncode, user);
    strcat(toEncode, ":");
    strcat(toEncode, pwd);
    char authorization[base64_encode_expected_len(strlen(toEncode)) + 1];
    authorization[0] = '\0';
    b64encode((const char*)toEncode, strlen(toEncode), authorization);

    // initializationsequence
    int16_t pos_slash;                                        // position of "/" in hostname
    int16_t pos_colon;                                        // position of ":" in hostname
    int16_t pos_ampersand;                                    // position of "&" in hostname
    uint16_t port = 80;                                       // port number
    m_f_webstream = true;
    setDatamode(AUDIO_HEADER);                                // Handle header

    h_host = l_host;

    if(startsWith(l_host, "http://")) {
        h_host += 7;
        m_f_ssl = false;
        _client = static_cast<WiFiClient*>(&client);
    }

    if(startsWith(l_host, "https://")) {
        h_host += 8;
        m_f_ssl = true;
        port = 443;
        _client = static_cast<WiFiClient*>(&clientsecure);
    }

    // Is it a playlist?
    if(endsWith(h_host, ".m3u" ))       {m_playlistFormat = FORMAT_M3U;  m_datamode = AUDIO_PLAYLISTINIT;}
    if(endsWith(h_host, ".pls" ))       {m_playlistFormat = FORMAT_PLS;  m_datamode = AUDIO_PLAYLISTINIT;}
    if(endsWith(h_host, ".asx" ))       {m_playlistFormat = FORMAT_ASX;  m_datamode = AUDIO_PLAYLISTINIT;}
    // if url ...=asx   www.fantasyfoxradio.de/infusions/gr_radiostatus_panel/gr_radiostatus_player.php?id=2&p=asx
    if(endsWith(h_host, "=asx" ))       {m_playlistFormat = FORMAT_ASX;  m_datamode = AUDIO_PLAYLISTINIT;}
    if(endsWith(h_host, "=pls" ))       {m_playlistFormat = FORMAT_PLS;  m_datamode = AUDIO_PLAYLISTINIT;}
    // if url  "http://n3ea-e2.revma.ihrhls.com/zc7729/hls.m3u8?rj-ttl=5&rj-tok=AAABe8unpPAAyu36Dkm0J4mzJg"
    if(indexOf(h_host, ".m3u8", 0) >10) {m_playlistFormat = FORMAT_M3U8; m_datamode = AUDIO_PLAYLISTINIT;}

    // In the URL there may be an extension, like noisefm.ru:8000/play.m3u&t=.m3u
    pos_slash     = indexOf(h_host, "/", 0);
    pos_colon     = indexOf(h_host, ":", 0);
    pos_ampersand = indexOf(h_host, "&", 0);

    char *hostwoext = NULL;                                  // "skonto.ls.lv:8002" in "skonto.ls.lv:8002/mp3"
    char *extension = NULL;                                  // "/mp3" in "skonto.ls.lv:8002/mp3"

    if(pos_slash > 1) {
        uint8_t hostwoextLen = pos_slash;
        hostwoext = (char*)malloc(hostwoextLen + 1);
        memcpy(hostwoext, h_host, hostwoextLen);
        hostwoext[hostwoextLen] = '\0';
        uint16_t extLen =  urlencode_expected_len(h_host + pos_slash);
        extension = (char *)malloc(extLen);
        memcpy(extension, h_host + pos_slash, extLen);
        trim(extension);
        urlencode(extension, extLen, true);
    }
    else{  // url has no extension
        hostwoext = strdup(h_host);
        extension = strdup("/");
    }

    if((pos_colon >= 0) && ((pos_ampersand == -1) or (pos_ampersand > pos_colon))){
        port = atoi(h_host + pos_colon + 1);// Get portnumber as integer
        hostwoext[pos_colon] = '\0';// Host without portnumber
    }

    AUDIO_INFO(sprintf(chbuf, "Connect to \"%s\" on port %d, extension \"%s\"", hostwoext, port, extension);)

    char resp[strlen(h_host) + strlen(authorization) + 100];
    resp[0] = '\0';

    strcat(resp, "GET ");
    strcat(resp, extension);
    strcat(resp, " HTTP/1.0\r\n");
    strcat(resp, "Host: ");
    strcat(resp, hostwoext);
    strcat(resp, "\r\n");
    strcat(resp, "Icy-MetaData:1\r\n");
    strcat(resp, "Authorization: Basic ");
    strcat(resp, authorization);
    strcat(resp, "\r\n");
    strcat(resp, "User-Agent: Mozilla/5.0\r\n");
//    strcat(resp, "Accept-Encoding: gzip;q=0\r\n");  // otherwise the server assumes gzip compression
//    strcat(resp, "Transfer-Encoding: \r\n");  // otherwise the server assumes gzip compression
    strcat(resp, "Connection: keep-alive\r\n\r\n");
    uint32_t t = millis();
    if(_client->connect(hostwoext, port, m_f_ssl ? m_timeout_ms_ssl : m_timeout_ms)) {
        if(!m_f_ssl) _client->setNoDelay(true);
        // if(audio_info) audio_info("SSL/TLS Connected to server");
        _client->print(resp);
        uint32_t dt = millis() - t;
        AUDIO_INFO(sprintf(chbuf, "%s has been established in %u ms, free Heap: %u bytes", m_f_ssl?"SSL":"Connection", dt, ESP.getFreeHeap());)
        strcpy(m_lastHost, l_host);
        m_f_running = true;
        if(hostwoext) {free(hostwoext); hostwoext = NULL;}
        if(extension) {free(extension); extension = NULL;}
        if(l_host   ) {free(l_host);    l_host    = NULL;}
        while(!_client->connected()){;} // wait until the connection is established
        return true;
    }
    AUDIO_INFO(sprintf(chbuf, "Request %s failed!", l_host);)
    if(audio_showstation) audio_showstation("");
    if(audio_showstreamtitle) audio_showstreamtitle("");
    if(audio_icydescription) audio_icydescription("");
    if(audio_icyurl) audio_icyurl("");
    m_lastHost[0] = 0;
    if(hostwoext) {free(hostwoext); hostwoext = NULL;}
    if(extension) {free(extension); extension = NULL;}
    if(l_host   ) {free(l_host);    l_host    = NULL;}
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setFileLoop(bool input){
    m_f_loop = input;
    return input;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::UTF8toASCII(char* str){

#ifdef SDFATFS_USED
    //UTF8->UTF16 (lowbyte)
    const uint8_t ascii[60] = {
    //129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148  // UTF8(C3)
    //                Ä    Å    Æ    Ç         É                                       Ñ                  // CHAR
      000, 000, 000, 0xC4, 143, 0xC6,0xC7, 000,0xC9,000, 000, 000, 000, 000, 000, 000, 0xD1, 000, 000, 000, // ASCII (Latin1)
    //149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168
    //      Ö                             Ü              ß    à                   ä    å    æ         è
      000, 0xD6,000, 000, 000, 000, 000, 0xDC, 000, 000, 0xDF,0xE0, 000, 000, 000,0xE4,0xE5,0xE6, 000,0xE8,
    //169, 170, 171, 172. 173. 174. 175, 176, 177, 179, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188
    //      ê    ë    ì         î    ï         ñ    ò         ô         ö              ù         û    ü
      000, 0xEA, 0xEB,0xEC, 000,0xEE,0xEB, 000,0xF1,0xF2, 000,0xF4, 000,0xF6, 000, 000,0xF9, 000,0xFB,0xFC};
#else
    const uint8_t ascii[60] = {
    //129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148  // UTF8(C3)
    //                Ä    Å    Æ    Ç         É                                       Ñ                  // CHAR
      000, 000, 000, 142, 143, 146, 128, 000, 144, 000, 000, 000, 000, 000, 000, 000, 165, 000, 000, 000, // ASCII
    //149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168
    //      Ö                             Ü              ß    à                   ä    å    æ         è
      000, 153, 000, 000, 000, 000, 000, 154, 000, 000, 225, 133, 000, 000, 000, 132, 134, 145, 000, 138,
    //169, 170, 171, 172. 173. 174. 175, 176, 177, 179, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188
    //      ê    ë    ì         î    ï         ñ    ò         ô         ö              ù         û    ü
      000, 136, 137, 141, 000, 140, 139, 000, 164, 149, 000, 147, 000, 148, 000, 000, 151, 000, 150, 129};
#endif

    uint16_t i = 0, j=0, s = 0;
    bool f_C3_seen = false;

    while(str[i] != 0) {                                     // convert UTF8 to ASCII
        if(str[i] == 195){                                   // C3
            i++;
            f_C3_seen = true;
            continue;
        }
        str[j] = str[i];
        if(str[j] > 128 && str[j] < 189 && f_C3_seen == true) {
            s = ascii[str[j] - 129];
            if(s != 0) str[j] = s;                         // found a related ASCII sign
            f_C3_seen = false;
        }
        i++; j++;
    }
    str[j] = 0;
}
#ifndef AUDIO_NO_SD_FS
//---------------------------------------------------------------------------------------------------------------------
bool Audio::connecttoSD(const char* path, uint32_t resumeFilePos) {
    return connecttoFS(SD, path, resumeFilePos);
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::connecttoFS(fs::FS &fs, const char* path, uint32_t resumeFilePos) {

    if(strlen(path)>255) return false;

    m_resumeFilePos = resumeFilePos;
    char audioName[256];
    setDefaults(); // free buffers an set defaults
    memcpy(audioName, path, strlen(path)+1);
    if(audioName[0] != '/'){
        for(int i = 255; i > 0; i--){
            audioName[i] = audioName[i-1];
        }
        audioName[0] = '/';
    }

    AUDIO_INFO(sprintf(chbuf, "Reading file: \"%s\"", audioName);vTaskDelay(2);)

    if(fs.exists(audioName)) {
        audiofile = fs.open(audioName); // #86
    }
    else {
        UTF8toASCII(audioName);
        if(fs.exists(audioName)) {
            audiofile = fs.open(audioName);
        }
    }

    if(!audiofile) {
        if(audio_info) {vTaskDelay(2); audio_info("Failed to open file for reading");}
        return false;
    }

    m_f_localfile = true;
    m_file_size = audiofile.size();//TEST loop

    char* afn = NULL;  // audioFileName

#ifdef SDFATFS_USED
    audiofile.getName(chbuf, sizeof(chbuf));
    afn = strdup(chbuf);
#else
    afn = strdup(audiofile.name());
#endif

    uint8_t dotPos = lastIndexOf(afn, ".");
    for(uint8_t i = dotPos + 1; i < strlen(afn); i++){
        afn[i] = toLowerCase(afn[i]);
    }

    if(endsWith(afn, ".mp3")){      // MP3 section
        free(afn);
        m_codec = CODEC_MP3;
        if(!MP3Decoder_AllocateBuffers()){audiofile.close(); return false;}
        InBuff.changeMaxBlockSize(m_frameSizeMP3);
        AUDIO_INFO(sprintf(chbuf, "MP3Decoder has been initialized, free Heap: %u bytes", ESP.getFreeHeap());)
        m_f_running = true;
        return true;
    } // end MP3 section

    AUDIO_INFO(sprintf(chbuf, "The %s format is not supported", afn + dotPos);)
    audiofile.close();
    if(afn) free(afn);
    return false;
}
#endif // AUDIO_NO_SD_FS
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
void Audio::urlencode(char* buff, uint16_t buffLen, bool spacesOnly) {

    uint16_t len = strlen(buff);
    uint8_t* tmpbuff = (uint8_t*)malloc(buffLen);
    if(!tmpbuff) {log_e("out of memory"); return;}
    char c;
    char code0;
    char code1;
    uint16_t j = 0;
    for(int i = 0; i < len; i++) {
        c = buff[i];
        if(isalnum(c)) tmpbuff[j++] = c;
        else if(spacesOnly){
            if(c == ' '){
                tmpbuff[j++] = '%';
                tmpbuff[j++] = '2';
                tmpbuff[j++] = '0';
            }
            else{
                tmpbuff[j++] = c;
            }
        }
        else {
            code1 = (c & 0xf) + '0';
            if((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if(c > 9) code0 = c - 10 + 'A';
            tmpbuff[j++] = '%';
            tmpbuff[j++] = code0;
            tmpbuff[j++] = code1;
        }
        if(j == buffLen - 1){
            log_e("out of memory");
            break;
        }
    }
    memcpy(buff, tmpbuff, j);
    buff[j] ='\0';
    free(tmpbuff);
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::showID3Tag(const char* tag, const char* value){

    chbuf[0] = 0;
    // V2.2
    if(!strcmp(tag, "CNT")) sprintf(chbuf, "Play counter: %s", value);
    // if(!strcmp(tag, "COM")) sprintf(chbuf, "Comments: %s", value);
    if(!strcmp(tag, "CRA")) sprintf(chbuf, "Audio encryption: %s", value);
    if(!strcmp(tag, "CRM")) sprintf(chbuf, "Encrypted meta frame: %s", value);
    if(!strcmp(tag, "ETC")) sprintf(chbuf, "Event timing codes: %s", value);
    if(!strcmp(tag, "EQU")) sprintf(chbuf, "Equalization: %s", value);
    if(!strcmp(tag, "IPL")) sprintf(chbuf, "Involved people list: %s", value);
    if(!strcmp(tag, "PIC")) sprintf(chbuf, "Attached picture: %s", value);
    if(!strcmp(tag, "SLT")) sprintf(chbuf, "Synchronized lyric/text: %s", value);
    // if(!strcmp(tag, "TAL")) sprintf(chbuf, "Album/Movie/Show title: %s", value);
    if(!strcmp(tag, "TBP")) sprintf(chbuf, "BPM (Beats Per Minute): %s", value);
    if(!strcmp(tag, "TCM")) sprintf(chbuf, "Composer: %s", value);
    if(!strcmp(tag, "TCO")) sprintf(chbuf, "Content type: %s", value);
    if(!strcmp(tag, "TCR")) sprintf(chbuf, "Copyright message: %s", value);
    if(!strcmp(tag, "TDA")) sprintf(chbuf, "Date: %s", value);
    if(!strcmp(tag, "TDY")) sprintf(chbuf, "Playlist delay: %s", value);
    if(!strcmp(tag, "TEN")) sprintf(chbuf, "Encoded by: %s", value);
    if(!strcmp(tag, "TFT")) sprintf(chbuf, "File type: %s", value);
    if(!strcmp(tag, "TIM")) sprintf(chbuf, "Time: %s", value);
    if(!strcmp(tag, "TKE")) sprintf(chbuf, "Initial key: %s", value);
    if(!strcmp(tag, "TLA")) sprintf(chbuf, "Language(s): %s", value);
    if(!strcmp(tag, "TLE")) sprintf(chbuf, "Length: %s", value);
    if(!strcmp(tag, "TMT")) sprintf(chbuf, "Media type: %s", value);
    if(!strcmp(tag, "TOA")) sprintf(chbuf, "Original artist(s)/performer(s): %s", value);
    if(!strcmp(tag, "TOF")) sprintf(chbuf, "Original filename: %s", value);
    if(!strcmp(tag, "TOL")) sprintf(chbuf, "Original Lyricist(s)/text writer(s): %s", value);
    if(!strcmp(tag, "TOR")) sprintf(chbuf, "Original release year: %s", value);
    if(!strcmp(tag, "TOT")) sprintf(chbuf, "Original album/Movie/Show title: %s", value);
    if(!strcmp(tag, "TP1")) sprintf(chbuf, "Lead artist(s)/Lead performer(s)/Soloist(s)/Performing group: %s", value);
    if(!strcmp(tag, "TP2")) sprintf(chbuf, "Band/Orchestra/Accompaniment: %s", value);
    if(!strcmp(tag, "TP3")) sprintf(chbuf, "Conductor/Performer refinement: %s", value);
    if(!strcmp(tag, "TP4")) sprintf(chbuf, "Interpreted, remixed, or otherwise modified by: %s", value);
    if(!strcmp(tag, "TPA")) sprintf(chbuf, "Part of a set: %s", value);
    if(!strcmp(tag, "TPB")) sprintf(chbuf, "Publisher: %s", value);
    if(!strcmp(tag, "TRC")) sprintf(chbuf, "ISRC (International Standard Recording Code): %s", value);
    if(!strcmp(tag, "TRD")) sprintf(chbuf, "Recording dates: %s", value);
    if(!strcmp(tag, "TRK")) sprintf(chbuf, "Track number/Position in set: %s", value);
    if(!strcmp(tag, "TSI")) sprintf(chbuf, "Size: %s", value);
    if(!strcmp(tag, "TSS")) sprintf(chbuf, "Software/hardware and settings used for encoding: %s", value);
    if(!strcmp(tag, "TT1")) sprintf(chbuf, "Content group description: %s", value);
    if(!strcmp(tag, "TT2")) sprintf(chbuf, "Title/Songname/Content description: %s", value);
    if(!strcmp(tag, "TT3")) sprintf(chbuf, "Subtitle/Description refinement: %s", value);
    if(!strcmp(tag, "TXT")) sprintf(chbuf, "Lyricist/text writer: %s", value);
    if(!strcmp(tag, "TXX")) sprintf(chbuf, "User defined text information frame: %s", value);
    if(!strcmp(tag, "TYE")) sprintf(chbuf, "Year: %s", value);
    if(!strcmp(tag, "UFI")) sprintf(chbuf, "Unique file identifier: %s", value);
    if(!strcmp(tag, "ULT")) sprintf(chbuf, "Unsychronized lyric/text transcription: %s", value);
    if(!strcmp(tag, "WAF")) sprintf(chbuf, "Official audio file webpage: %s", value);
    if(!strcmp(tag, "WAR")) sprintf(chbuf, "Official artist/performer webpage: %s", value);
    if(!strcmp(tag, "WAS")) sprintf(chbuf, "Official audio source webpage: %s", value);
    if(!strcmp(tag, "WCM")) sprintf(chbuf, "Commercial information: %s", value);
    if(!strcmp(tag, "WCP")) sprintf(chbuf, "Copyright/Legal information: %s", value);
    if(!strcmp(tag, "WPB")) sprintf(chbuf, "Publishers official webpage: %s", value);
    if(!strcmp(tag, "WXX")) sprintf(chbuf, "User defined URL link frame: %s", value);

    // V2.3 V2.4 tags
    // if(!strcmp(tag, "COMM")) sprintf(chbuf, "Comment: %s", value);
    if(!strcmp(tag, "OWNE")) sprintf(chbuf, "Ownership: %s", value);
    // if(!strcmp(tag, "PRIV")) sprintf(chbuf, "Private: %s", value);
    if(!strcmp(tag, "SYLT")) sprintf(chbuf, "SynLyrics: %s", value);
    if(!strcmp(tag, "TALB")) sprintf(chbuf, "Album: %s", value);
    if(!strcmp(tag, "TBPM")) sprintf(chbuf, "BeatsPerMinute: %s", value);
    if(!strcmp(tag, "TCMP")) sprintf(chbuf, "Compilation: %s", value);
    if(!strcmp(tag, "TCOM")) sprintf(chbuf, "Composer: %s", value);
    if(!strcmp(tag, "TCON")) sprintf(chbuf, "ContentType: %s", value);
    if(!strcmp(tag, "TCOP")) sprintf(chbuf, "Copyright: %s", value);
    if(!strcmp(tag, "TDAT")) sprintf(chbuf, "Date: %s", value);
    if(!strcmp(tag, "TEXT")) sprintf(chbuf, "Lyricist: %s", value);
    if(!strcmp(tag, "TIME")) sprintf(chbuf, "Time: %s", value);
    if(!strcmp(tag, "TIT1")) sprintf(chbuf, "Grouping: %s", value);
    if(!strcmp(tag, "TIT2")) sprintf(chbuf, "Title: %s", value);
    if(!strcmp(tag, "TIT3")) sprintf(chbuf, "Subtitle: %s", value);
    if(!strcmp(tag, "TLAN")) sprintf(chbuf, "Language: %s", value);
    if(!strcmp(tag, "TLEN")) sprintf(chbuf, "Length (ms): %s", value);
    if(!strcmp(tag, "TMED")) sprintf(chbuf, "Media: %s", value);
    if(!strcmp(tag, "TOAL")) sprintf(chbuf, "OriginalAlbum: %s", value);
    if(!strcmp(tag, "TOPE")) sprintf(chbuf, "OriginalArtist: %s", value);
    if(!strcmp(tag, "TORY")) sprintf(chbuf, "OriginalReleaseYear: %s", value);
    if(!strcmp(tag, "TPE1")) sprintf(chbuf, "Artist: %s", value);
    if(!strcmp(tag, "TPE2")) sprintf(chbuf, "Band: %s", value);
    if(!strcmp(tag, "TPE3")) sprintf(chbuf, "Conductor: %s", value);
    if(!strcmp(tag, "TPE4")) sprintf(chbuf, "InterpretedBy: %s", value);
    if(!strcmp(tag, "TPOS")) sprintf(chbuf, "PartOfSet: %s", value);
    if(!strcmp(tag, "TPUB")) sprintf(chbuf, "Publisher: %s", value);
    if(!strcmp(tag, "TRCK")) sprintf(chbuf, "Track: %s", value);
    if(!strcmp(tag, "TSSE")) sprintf(chbuf, "SettingsForEncoding: %s", value);
    if(!strcmp(tag, "TRDA")) sprintf(chbuf, "RecordingDates: %s", value);
    if(!strcmp(tag, "TXXX")) sprintf(chbuf, "UserDefinedText: %s", value);
    if(!strcmp(tag, "TYER")) sprintf(chbuf, "Year: %s", value);
    if(!strcmp(tag, "USER")) sprintf(chbuf, "TermsOfUse: %s", value);
    if(!strcmp(tag, "USLT")) sprintf(chbuf, "Lyrics: %s", value);
    if(!strcmp(tag, "WOAR")) sprintf(chbuf, "OfficialArtistWebpage: %s", value);
    if(!strcmp(tag, "XDOR")) sprintf(chbuf, "OriginalReleaseTime: %s", value);

    latinToUTF8(chbuf, sizeof(chbuf));
    if(chbuf[0] != 0) if(audio_id3data) audio_id3data(chbuf);
}
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
int Audio::read_MP3_Header(uint8_t *data, size_t len) {

    static size_t id3Size;
    static size_t headerSize;
    static uint8_t ID3version;
    static int ehsz = 0;
    static char tag[5];
    static char frameid[5];
    static size_t framesize = 0;
    static bool compressed = false;
    static bool APIC_seen = false;
    static size_t APIC_size = 0;
    static uint32_t APIC_pos = 0;
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 0){      /* read ID3 tag and ID3 header size */
        if(m_f_localfile){
            ID3version = 0;
            m_contentlength = getFileSize();
            AUDIO_INFO(sprintf(chbuf, "Content-Length: %u", m_contentlength);)
        }
        m_controlCounter ++;
        APIC_seen = false;
        headerSize = 0;
        ehsz = 0;
        if(specialIndexOf(data, "ID3", 4) != 0) { // ID3 not found
            if(audio_info) audio_info("file has no mp3 tag, skip metadata");
            m_audioDataSize = m_contentlength;
            AUDIO_INFO(sprintf(chbuf, "Audio-Length: %u", m_audioDataSize);)
            return -1; // error, no ID3 signature found
        }
        ID3version = *(data + 3);
        switch(ID3version){
            case 2:
                m_f_unsync = (*(data + 5) & 0x80);
                m_f_exthdr = false;
                break;
            case 3:
            case 4:
                m_f_unsync = (*(data + 5) & 0x80); // bit7
                m_f_exthdr = (*(data + 5) & 0x40); // bit6 extended header
                break;
        };
        id3Size = bigEndian(data + 6, 4, 7); //  ID3v2 size  4 * %0xxxxxxx (shift left seven times!!)
        id3Size += 10;

        // Every read from now may be unsync'd
        AUDIO_INFO(sprintf(chbuf, "ID3 framesSize: %i", id3Size);)

        AUDIO_INFO(sprintf(chbuf, "ID3 version: 2.%i", ID3version);)

        if(ID3version == 2){
            m_controlCounter = 10;
        }
        headerSize = id3Size;
        headerSize -= 10;

        return 10;
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 1){      // compute extended header size if exists
        m_controlCounter ++;
        if(m_f_exthdr) {
            if(audio_info) audio_info("ID3 extended header");
            ehsz =  bigEndian(data, 4);
            headerSize -= 4;
            ehsz -= 4;
            return 4;
        }
        else{
            if(audio_info) audio_info("ID3 normal frames");
            return 0;
        }
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 2){      // skip extended header if exists
        if(ehsz > 256) {
            ehsz -=256;
            headerSize -= 256;
            return 256;} // Throw it away
        else           {
            m_controlCounter ++;
            headerSize -= ehsz;
            return ehsz;} // Throw it away
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 3){      // read a ID3 frame, get the tag
        if(headerSize == 0){
            m_controlCounter = 99;
            return 0;
        }
        m_controlCounter ++;
        frameid[0] = *(data + 0);
        frameid[1] = *(data + 1);
        frameid[2] = *(data + 2);
        frameid[3] = *(data + 3);
        frameid[4] = 0;
        for(uint8_t i = 0; i < 4; i++) tag[i] = frameid[i]; // tag = frameid

        headerSize -= 4;
        if(frameid[0] == 0 && frameid[1] == 0 && frameid[2] == 0 && frameid[3] == 0) {
            // We're in padding
            m_controlCounter = 98;  // all ID3 metadata processed
        }
        return 4;
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 4){  // get the frame size
        m_controlCounter = 6;

        if(ID3version == 4){
            framesize = bigEndian(data, 4, 7); // << 7
        }
        else {
            framesize = bigEndian(data, 4);  // << 8
        }
        headerSize -= 4;
        uint8_t flag = *(data + 4); // skip 1st flag
        (void) flag;
        headerSize--;
        compressed = (*(data + 5)) & 0x80; // Frame is compressed using [#ZLIB zlib] with 4 bytes for 'decompressed
                                           // size' appended to the frame header.
        headerSize--;
        uint32_t decompsize = 0;
        if(compressed){
            log_i("iscompressed");
            decompsize = bigEndian(data + 6, 4);
            headerSize -= 4;
            (void) decompsize;
            log_i("decompsize=%u", decompsize);
            return 6 + 4;
        }
        return 6;
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 5){      // If the frame is larger than 256 bytes, skip the rest
        if(framesize > 256){
            framesize -= 256;
            headerSize -= 256;
            return 256;
        }
        else {
            m_controlCounter = 3; // check next frame
            headerSize -= framesize;
            return framesize;
        }
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 6){      // Read the value
        m_controlCounter = 5;       // only read 256 bytes
        char value[256];
        char ch = *(data + 0);
        bool isUnicode = (ch==1) ? true : false;

        if(startsWith(tag, "APIC")) { // a image embedded in file, passing it to external function
            //log_i("framesize=%i", framesize);
            isUnicode = false;
            if(m_f_localfile){
                APIC_seen = true;
                APIC_pos = id3Size - headerSize;
                APIC_size = framesize;
            }
            return 0;
        }

        size_t fs = framesize;
        if(fs >255) fs = 255;
        for(int i=0; i<fs; i++){
            value[i] = *(data + i);
        }
        framesize -= fs;
        headerSize -= fs;
        value[fs] = 0;
        if(isUnicode && fs > 1) {
            unicode2utf8(value, fs);   // convert unicode to utf-8 U+0020...U+07FF
        }
        if(!isUnicode){
            uint16_t j = 0, k = 0;
            j = 0;
            k = 0;
            while(j < fs) {
                if(value[j] == 0x0A) value[j] = 0x20; // replace LF by space
                if(value[j] > 0x1F) {
                    value[k] = value[j];
                    k++;
                }
                j++;
            } //remove non printables
            if(k>0) value[k] = 0; else value[0] = 0; // new termination
        }
        showID3Tag(tag, value);
        return fs;
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    // -- section V2.2 only , greater Vers above ----
    if(m_controlCounter == 10){ // frames in V2.2, 3bytes identifier, 3bytes size descriptor
        frameid[0] = *(data + 0);
        frameid[1] = *(data + 1);
        frameid[2] = *(data + 2);
        frameid[3] = 0;
        for(uint8_t i = 0; i < 4; i++) tag[i] = frameid[i]; // tag = frameid
        headerSize -= 3;
        size_t len = bigEndian(data + 3, 3);
        headerSize -= 3;
        headerSize -= len;
        char value[256];
        size_t tmp = len;
        if(tmp > 254) tmp = 254;
        memcpy(value, (data + 7), tmp);
        value[tmp+1] = 0;
        chbuf[0] = 0;

        showID3Tag(tag, value);
        if(len == 0) m_controlCounter = 98;

        return 3 + 3 + len;
    }
    // -- end section V2.2 -----------

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 98){ // skip all ID3 metadata (mostly spaces)
        if(headerSize > 256) {
            headerSize -=256;
            return 256;
        } // Throw it away
        else           {
            m_controlCounter = 99;
            return headerSize;
        } // Throw it away
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_controlCounter == 99){ //  exist another ID3tag?
        m_audioDataStart += id3Size;
        vTaskDelay(30);
        if((*(data + 0) == 'I') && (*(data + 1) == 'D') && (*(data + 2) == '3')) {
            m_controlCounter = 0;
            return 0;
        }
        else {
            m_controlCounter = 100; // ok
            m_audioDataSize = m_contentlength - m_audioDataStart;
            AUDIO_INFO(sprintf(chbuf, "Audio-Length: %u", m_audioDataSize);)
#ifndef AUDIO_NO_SD_FS
            if(APIC_seen && audio_id3image){
                size_t pos = audiofile.position();
                audio_id3image(audiofile, APIC_pos, APIC_size);
                audiofile.seek(pos); // the filepointer could have been changed by the user, set it back
            }
#endif // AUDIO_NO_SD_FS
            return 0;
        }
    }
    return 0;
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::stopSong() {
    uint32_t pos = 0;
    if(m_f_running) {
        m_f_running = false;
#ifndef AUDIO_NO_SD_FS
        if(m_f_localfile){
            m_f_localfile = false;
            pos = getFilePos() - inBufferFilled();
            audiofile.close();
            if(audio_info) audio_info("Closing audio file");
        }
#endif // AUDIO_NO_SD_FS
    }
#ifndef AUDIO_NO_SD_FS
    if(audiofile){
        // added this before putting 'm_f_localfile = false' in stopSong(); shoulf never occur....
        audiofile.close();
        if(audio_info) audio_info("Closing audio file");
        log_w("Closing audio file");  // for debug
    }
#endif                                           // AUDIO_NO_SD_FS
    memset(m_outBuff, 0, sizeof(m_outBuff));     //Clear OutputBuffer
    i2s_zero_dma_buffer((i2s_port_t) m_i2s_num);
    return pos;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::playI2Sremains() { // returns true if all dma_buffs flushed
    if(!getSampleRate()) setSampleRate(96000);
    if(!getChannels()) setChannels(2);
    if(getBitsPerSample() > 8) memset(m_outBuff,   0, sizeof(m_outBuff));     //Clear OutputBuffer (signed)
    else                       memset(m_outBuff, 128, sizeof(m_outBuff));     //Clear OutputBuffer (unsigned, PCM 8u)

    m_validSamples = m_i2s_config.dma_buf_len;
    while(m_validSamples) {
        playChunk();
    }
    return;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::pauseResume() {
    bool retVal = false;
    if(m_f_localfile || m_f_webstream) {
        m_f_running = !m_f_running;
        retVal = true;
        if(!m_f_running) {
            memset(m_outBuff, 0, sizeof(m_outBuff));               //Clear OutputBuffer
            i2s_zero_dma_buffer((i2s_port_t) m_i2s_num);
        }
    }
    return retVal;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::playChunk() {
    // If we've got data, try and pump it out..
    int16_t sample[2];
    if(getBitsPerSample() == 8) {
        if(getChannels() == 1) {
            while(m_validSamples) {
                uint8_t x =  m_outBuff[m_curSample] & 0x00FF;
                uint8_t y = (m_outBuff[m_curSample] & 0xFF00) >> 8;
                sample[LEFTCHANNEL]  = x;
                sample[RIGHTCHANNEL] = x;
                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                sample[LEFTCHANNEL]  = y;
                sample[RIGHTCHANNEL] = y;
                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                m_validSamples--;
                m_curSample++;
            }
        }
        if(getChannels() == 2) {
            while(m_validSamples) {
                uint8_t x =  m_outBuff[m_curSample] & 0x00FF;
                uint8_t y = (m_outBuff[m_curSample] & 0xFF00) >> 8;
                if(!m_f_forceMono) { // stereo mode
                    sample[LEFTCHANNEL]  = x;
                    sample[RIGHTCHANNEL] = y;
                }
                else { // force mono
                    uint8_t xy = (x + y) / 2;
                    sample[LEFTCHANNEL]  = xy;
                    sample[RIGHTCHANNEL] = xy;
                }

                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                m_validSamples--;
                m_curSample++;
            }
        }
        m_curSample = 0;
        return true;
    }
    if(getBitsPerSample() == 16) {
        if(getChannels() == 1) {
            while(m_validSamples) {
                sample[LEFTCHANNEL]  = m_outBuff[m_curSample];
                sample[RIGHTCHANNEL] = m_outBuff[m_curSample];
                if(!playSample(sample)) {
                    return false;
                } // Can't send
                m_validSamples--;
                m_curSample++;
            }
        }
        if(getChannels() == 2) {
            while(m_validSamples) {
                if(!m_f_forceMono) { // stereo mode
                    sample[LEFTCHANNEL]  = m_outBuff[m_curSample * 2];
                    sample[RIGHTCHANNEL] = m_outBuff[m_curSample * 2 + 1];
                }
                else { // mono mode, #100
                    int16_t xy = (m_outBuff[m_curSample * 2] + m_outBuff[m_curSample * 2 + 1]) / 2;
                    sample[LEFTCHANNEL] = xy;
                    sample[RIGHTCHANNEL] = xy;
                }
                if(!playSample(sample)) {
                    return false;
                } // Can't send
                m_validSamples--;
                m_curSample++;
            }
        }
        m_curSample = 0;
        return true;
    }
    log_e("BitsPer Sample must be 8 or 16!");
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::loop() {
#ifndef AUDIO_NO_SD_FS
    // - localfile - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_f_localfile) {                                      // Playing file fron SPIFFS or SD?
        processLocalFile();
    }
#endif // AUDIO_NO_SD_FS
    return;
}
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
bool Audio::STfromEXTINF(char* str){
    // extraxt StreamTitle from m3u #EXTINF line to icy-format
    // orig: #EXTINF:10,title="text="TitleName",artist="ArtistName"
    // conv: StreamTitle=TitleName - ArtistName
    // orig: #EXTINF:10,title="text=\"Spot Block End\" amgTrackId=\"9876543\"",artist=" ",url="length=\"00:00:00\""
    // conv: StreamTitle=text=\"Spot Block End\" amgTrackId=\"9876543\" -

    if(!startsWith(str,"#EXTINF")) return false;
    int t1, t2, t3, n0 = 0, n1 = 0, n2 = 0;

    t1 = indexOf(str, "title", 0);
    if(t1 > 0){
        strcpy(chbuf, "StreamTitle="); n0 = 12;
        t2 = t1 + 7; // title="
        t3 = indexOf(str, "\"", t2);
        while(str[t3 - 1] == '\\'){
            t3 = indexOf(str, "\"", t3 + 1);
        }
        if(t2 < 0 || t2 > t3) return false;
        n1 = t3 - t2;
        strncpy(chbuf + n0, str + t2, n1);
    }

    t1 = indexOf(str, "artist", 0);
    if(t1 > 0){
        strcpy(chbuf + n0 + n1, " - ");   n1 += 3;
        t2 = indexOf(str, "=\"", t1); t2 += 2;
        t3 = indexOf(str, "\"", t2);
        if(t2 < 0 || t2 > t3) return false;
        n2 = t3 - t2;
        strncpy(chbuf + n0 + n1, str + t2, n2);
        chbuf[n0 + n1 + n2] = '\0';
    }
    strcpy(str, chbuf);

    return true;
}
#ifndef AUDIO_NO_SD_FS
//---------------------------------------------------------------------------------------------------------------------
void Audio::processLocalFile() {

    if(!(audiofile && m_f_running && m_f_localfile)) return;

    int bytesDecoded = 0;
    uint32_t bytesCanBeWritten = 0;
    uint32_t bytesCanBeRead = 0;
    int32_t bytesAddedToBuffer = 0;
    static bool f_stream;

    if(m_f_firstCall) {  // runs only one time per connection, prepare for start
        m_f_firstCall = false;
        f_stream = false;
        return;
    }

    if(!f_stream && m_controlCounter == 100) {
        f_stream = true;
        if(audio_info) audio_info("stream ready");
        if(m_resumeFilePos){
            if(m_resumeFilePos < m_audioDataStart) m_resumeFilePos = m_audioDataStart;
            if(m_avr_bitrate) m_audioCurrentTime = ((m_resumeFilePos - m_audioDataStart) / m_avr_bitrate) * 8;
            audiofile.seek(m_resumeFilePos);
            InBuff.resetBuffer();
            log_i("m_resumeFilePos %i", m_resumeFilePos);
        }
    }

    bytesCanBeWritten = InBuff.writeSpace();
    //----------------------------------------------------------------------------------------------------
    // some files contain further data after the audio block (e.g. pictures).
    // In that case, the end of the audio block is not the end of the file. An 'eof' has to be forced.
    if((m_controlCounter == 100) && (m_contentlength > 0)) { // fileheader was read
           if(bytesCanBeWritten + getFilePos() >= m_contentlength){
               if(m_contentlength > getFilePos()) bytesCanBeWritten = m_contentlength - getFilePos();
               else bytesCanBeWritten = 0;
           }
    }
    //----------------------------------------------------------------------------------------------------

    bytesAddedToBuffer = audiofile.read(InBuff.getWritePtr(), bytesCanBeWritten);
    if(bytesAddedToBuffer > 0) {
        InBuff.bytesWritten(bytesAddedToBuffer);
    }

    if(bytesAddedToBuffer == -1) bytesAddedToBuffer = 0; // read error? eof?
    bytesCanBeRead = InBuff.bufferFilled();
    if(bytesCanBeRead > InBuff.getMaxBlockSize()) bytesCanBeRead = InBuff.getMaxBlockSize();
    if(bytesCanBeRead == InBuff.getMaxBlockSize()) { // mp3 or aac frame complete?

        if(m_controlCounter != 100){
            if(m_codec == CODEC_MP3){
                int res = read_MP3_Header(InBuff.getReadPtr(), bytesCanBeRead);
                if(res >= 0) bytesDecoded = res;
                else{ // error, skip header
                    m_controlCounter = 100;
                }
            }
            if(!isRunning()){
                log_e("Processing stopped due to invalid audio header");
                return;
            }
        }
        else {
            bytesDecoded = sendBytes(InBuff.getReadPtr(), bytesCanBeRead);
        }
        if(bytesDecoded > 0) {InBuff.bytesWasRead(bytesDecoded); return;}
        if(bytesDecoded < 0) {  // no syncword found or decode error, try next chunk
            InBuff.bytesWasRead(200); // try next chunk
            m_bytesNotDecoded += 200;
            return;
        }
        return;
    }

    if(!bytesAddedToBuffer) {  // eof
        bytesCanBeRead = InBuff.bufferFilled();
        if(bytesCanBeRead > 200){
            if(bytesCanBeRead > InBuff.getMaxBlockSize()) bytesCanBeRead = InBuff.getMaxBlockSize();
            bytesDecoded = sendBytes(InBuff.getReadPtr(), bytesCanBeRead); // play last chunk(s)
            if(bytesDecoded > 0){
                InBuff.bytesWasRead(bytesDecoded);
                return;
            }
        }
        InBuff.resetBuffer();
        playI2Sremains();

        if(m_f_loop  && f_stream){  //eof
            AUDIO_INFO(sprintf(chbuf, "loop from: %u to: %u", getFilePos(), m_audioDataStart);) //TEST loop
            setFilePos(m_audioDataStart);
            /*
                The current time of the loop mode is not reset,
                which will cause the total audio duration to be exceeded.
                For example: current time   ====progress bar====>  total audio duration
                                3:43        ====================>        3:33
            */
            m_audioCurrentTime = 0;
            return;
        } //TEST loop
        f_stream = false;
        m_f_localfile = false;

#ifdef SDFATFS_USED
        audiofile.getName(chbuf, sizeof(chbuf));
        char *afn =strdup(chbuf);
#else
        char *afn =strdup(audiofile.name()); // store temporary the name
#endif

        stopSong();
        if(m_codec == CODEC_MP3)   MP3Decoder_FreeBuffers();
        AUDIO_INFO(sprintf(chbuf, "End of file \"%s\"", afn);)
        if(audio_eof_mp3) audio_eof_mp3(afn);
        if(afn) free(afn);
    }
}
#endif // AUDIO_NO_SD_FS
//---------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------
void Audio::processAudioHeaderData() {

    int av = 0;
    av= _client->available();
    if(av <= 0) return;

    char hl[512]; // headerline
    uint8_t b = 0;
    uint16_t pos = 0;
    int16_t idx = 0;
    static bool f_icyname = false;
    static bool f_icydescription = false;
    static bool f_icyurl = false;

    while(true){
        b = _client->read();
        if(b == '\n') break;
        if(b == '\r') hl[pos] = 0;
        if(b < 0x20) continue;
        hl[pos] = b;
        pos++;
        if(pos == 510){
            hl[pos] = '\0';
            log_e("headerline overflow");
            break;
        }
    }

    if(!pos && m_f_ctseen){  // audio header complete?
        m_datamode = AUDIO_DATA;                         // Expecting data now
        if(m_f_Log) { AUDIO_INFO(sprintf(chbuf, "Switch to DATA, metaint is %d", m_metaint);) }
        memcpy(chbuf, m_lastHost, strlen(m_lastHost)+1);
//        uint idx = indexOf(chbuf, "?", 0);
//        if(idx > 0) chbuf[idx] = 0;
        if(audio_lasthost) audio_lasthost(chbuf);
        if(!f_icyname){if(audio_showstation) audio_showstation("");}
        if(!f_icydescription){if(audio_icydescription) audio_icydescription("");}
        if(!f_icyurl){if(audio_icyurl) audio_icyurl("");}
        f_icyname = false;
        f_icydescription = false;
        f_icyurl = false;
        delay(50);  // #77
        return;
    }
    if(!pos){
        stopSong();
        if(audio_showstation) audio_showstation("");
        if(audio_icydescription) audio_icydescription("");
        if(audio_icyurl) audio_icyurl("");
        f_icyname = false;
        f_icydescription = false;
        f_icyurl = false;
        log_e("can't see content in audioHeaderData");
        return;
    }

    idx = indexOf(hl, ":", 0); // lowercase all letters up to the colon
    if(idx >= 0) {
        for(int i=0; i< idx; i++) {
            hl[i] = toLowerCase(hl[i]);
        }
    }

    if(indexOf(hl, "HTTP/1.0 404", 0) >= 0) {
        m_f_running = false;
        stopSong();
        if(audio_info) audio_info("404 Not Found");
        return;
    }

    if(indexOf(hl, "HTTP/1.1 404", 0) >= 0) {
        m_f_running = false;
        stopSong();
        if(audio_info) audio_info("404 Not Found");
        return;
    }

    if(indexOf(hl, "ICY 401", 0) >= 0) {
        m_f_running = false;
        stopSong();
        if(audio_info) audio_info("ICY 401 Service Unavailable");
        return;
    }

    if(indexOf(hl, "content-type:", 0) >= 0) {
        if(parseContentType(hl)) m_f_ctseen = true;
    }
    else if(startsWith(hl, "location:")) {
        int pos = indexOf(hl, "http", 0);
        const char* c_host = (hl + pos);
        AUDIO_INFO(sprintf(chbuf, "redirect to new host \"%s\"", c_host);)
        connecttohost(c_host);
    }
    else if(startsWith(hl, "content-disposition:")) {
        int pos1, pos2; // pos3;
        // e.g we have this headerline:  content-disposition: attachment; filename=stream.asx
        // filename is: "stream.asx"
        pos1 = indexOf(hl, "filename=", 0);
        if(pos1 > 0){
            pos1 += 9;
            if(hl[pos1] == '\"') pos1++;  // remove '\"' around filename if present
            pos2 = strlen(hl);
            if(hl[pos2 - 1] == '\"') hl[pos2 - 1] = '\0';
        }
        log_d("Filename is %s", hl + pos1);
    }
    else if(startsWith(hl, "set-cookie:")    ||
            startsWith(hl, "pragma:")        ||
            startsWith(hl, "expires:")       ||
            startsWith(hl, "cache-control:") ||
            startsWith(hl, "icy-pub:")       ||
            startsWith(hl, "p3p:")           ||
            startsWith(hl, "accept-ranges:") ){
        ; // do nothing
    }
    else if(startsWith(hl, "connection:")) {
        if(indexOf(hl, "close", 0) >= 0) {; /* do nothing */}
    }
    else if(startsWith(hl, "icy-genre:")) {
        ; // do nothing Ambient, Rock, etc
    }
    else if(startsWith(hl, "icy-br:")) {
        const char* c_bitRate = (hl + 7);
        int32_t br = atoi(c_bitRate); // Found bitrate tag, read the bitrate in Kbit
        br = br * 1000;
        setBitrate(br);
        sprintf(chbuf, "%d", getBitRate());
        if(audio_bitrate) audio_bitrate(chbuf);
    }
    else if(startsWith(hl, "icy-metaint:")) {
        const char* c_metaint = (hl + 12);
        int32_t i_metaint = atoi(c_metaint);
        m_metaint = i_metaint;
        if(m_metaint) m_f_swm = false     ;                            // Multimediastream
    }
    else if(startsWith(hl, "icy-name:")) {
        char* c_icyname = (hl + 9); // Get station name
        idx = 0;
        while(c_icyname[idx] == ' '){idx++;} c_icyname += idx;        // Remove leading spaces
        idx = strlen(c_icyname);
        while(c_icyname[idx] == ' '){idx--;} c_icyname[idx + 1] = 0;  // Remove trailing spaces

        if(strlen(c_icyname) > 0) {
            AUDIO_INFO(sprintf(chbuf, "icy-name: %s", c_icyname);)
            if(audio_showstation) audio_showstation(c_icyname);
            f_icyname = true;
        }
    }
    else if(startsWith(hl, "content-length:")) {
        const char* c_cl = (hl + 15);
        int32_t i_cl = atoi(c_cl);
        m_contentlength = i_cl;
        m_f_webfile = true; // Stream comes from a fileserver
        if(m_f_Log) { AUDIO_INFO(sprintf(chbuf, "content-length: %i", m_contentlength);) }
    }
    else if(startsWith(hl, "icy-description:")) {
        const char* c_idesc = (hl + 16);
        while(c_idesc[0] == ' ') c_idesc++;
        latinToUTF8(hl, sizeof(hl)); // if already UTF-0 do nothing, otherwise convert to UTF-8
        if(audio_icydescription) audio_icydescription(c_idesc);
        f_icydescription = true;
    }
    else if((startsWith(hl, "transfer-encoding:"))){
        if(endsWith(hl, "chunked") || endsWith(hl, "Chunked") ) { // Station provides chunked transfer
            m_f_chunked = true;
            if(audio_info) audio_info("chunked data transfer");
            m_chunkcount = 0;                         // Expect chunkcount in DATA
        }
    }
    else if(startsWith(hl, "icy-url:")) {
        const char* icyurl = (hl + 8);
        idx = 0;
        while(icyurl[idx] == ' ') {idx ++;} icyurl += idx; // remove leading blanks
        //sprintf(chbuf, "icy-url: %s", icyurl);
        // if(audio_info) audio_info(chbuf);
        if(audio_icyurl) audio_icyurl(icyurl);
        f_icyurl = true;
    }
    else if(startsWith(hl, "www-authenticate:")) {
        if(audio_info) audio_info("authentification failed, wrong credentials?");
        m_f_running = false;
        stopSong();
    }
    else {
        if(isascii(hl[0]) && hl[0] >= 0x20) {  // all other
            if(m_f_Log) { AUDIO_INFO(sprintf(chbuf, "%s", hl);) }
        }
    }
    return;
}

//---------------------------------------------------------------------------------------------------------------------
bool Audio::readMetadata(uint8_t b, bool first) {

    static uint16_t pos_ml = 0;                          // determines the current position in metaline
    static uint16_t metalen = 0;
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(first){
        pos_ml = 0;
        metalen = 0;
        return true;
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(!metalen) {                                       // First byte of metadata?
        metalen = b * 16 + 1;                            // New count for metadata including length byte
        if(metalen >512){
            if(audio_info) audio_info("Metadata block to long! Skipping all Metadata from now on.");
            m_f_swm = true;                              // expect stream without metadata
        }
        pos_ml = 0; chbuf[pos_ml] = 0;                   // Prepare for new line
    }
    else {
        chbuf[pos_ml] = (char) b;                        // Put new char in +++++
        if(pos_ml < 510) pos_ml ++;
        chbuf[pos_ml] = 0;
        if(pos_ml == 509) { log_e("metaline overflow in AUDIO_METADATA! metaline=%s", chbuf); }
        if(pos_ml == 510) { ; /* last current char in b */}

    }
    if(--metalen == 0) {
        if(strlen(chbuf)) {                             // Any info present?
            // metaline contains artist and song name.  For example:
            // "StreamTitle='Don McLean - American Pie';StreamUrl='';"
            // Sometimes it is just other info like:
            // "StreamTitle='60s 03 05 Magic60s';StreamUrl='';"
            // Isolate the StreamTitle, remove leading and trailing quotes if present.
            // log_i("ST %s", metaline);

            latinToUTF8(chbuf, sizeof(chbuf)); // convert to UTF-8 if necessary

            int pos = indexOf(chbuf, "song_spot", 0);    // remove some irrelevant infos
            if(pos > 3) {                                // e.g. song_spot="T" MediaBaseId="0" itunesTrackId="0"
                chbuf[pos] = 0;
            }
            if(!m_f_localfile) showstreamtitle(chbuf);   // Show artist and title if present in metadata
        }
        return true ;
    }
    return false;// end_METADATA
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::parseContentType(const char* ct) {
    bool ct_seen = false;
    if(indexOf(ct, "audio", 0) >= 0) {        // Is ct audio?
        ct_seen = true;                       // Yes, remember seeing this
        if(indexOf(ct, "mpeg", 13) >= 0) {
            m_codec = CODEC_MP3;
            AUDIO_INFO(sprintf(chbuf, "%s, format is mp3", ct);) //ok is likely mp3
            if(!MP3Decoder_AllocateBuffers()) {m_f_running = false; stopSong(); return false;}
            AUDIO_INFO(sprintf(chbuf, "MP3Decoder has been initialized, free Heap: %u bytes", ESP.getFreeHeap());)
            InBuff.changeMaxBlockSize(m_frameSizeMP3);
        }
        else if(indexOf(ct, "mp3", 13) >= 0) {
            m_codec = CODEC_MP3;
            AUDIO_INFO(sprintf(chbuf, "%s, format is mp3", ct);)
            if(!MP3Decoder_AllocateBuffers()) {m_f_running = false; stopSong(); return false;}
            AUDIO_INFO(sprintf(chbuf, "MP3Decoder has been initialized, free Heap: %u bytes", ESP.getFreeHeap());)
            InBuff.changeMaxBlockSize(m_frameSizeMP3);
        }
        else {
            m_f_running = false;
            AUDIO_INFO(sprintf(chbuf, "%s, unsupported audio format", ct);)
        }
    }
    if(indexOf(ct, "application", 0) >= 0) {  // Is ct application?
        ct_seen = true;                       // Yes, remember seeing this
        uint8_t pos = indexOf(ct, "application", 0);
        if(indexOf(ct, "ogg", 13) >= 0) {
            m_codec = CODEC_OGG;
            AUDIO_INFO(sprintf(chbuf, "ContentType %s found", ct + pos);)
        }
    }
    return ct_seen;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::showstreamtitle(const char* ml) {
    // example for ml:
    // StreamTitle='Oliver Frank - Mega Hitmix';StreamUrl='www.radio-welle-woerthersee.at';
    // or adw_ad='true';durationMilliseconds='10135';adId='34254';insertionType='preroll';

    int16_t idx1, idx2;
    uint16_t i = 0, hash = 0;

    idx1 = indexOf(ml, "StreamTitle=", 0);
    if(idx1 >= 0){                                                              // Streamtitle found
        idx2 = indexOf(ml, ";", idx1);
        char *sTit;
        if(idx2 >= 0){sTit = strndup(ml + idx1, idx2 + 1); sTit[idx2] = '\0';}
        else          sTit =  strdup(ml);

        while(i < strlen(sTit)){hash += sTit[i] * i+1; i++;}

        if(m_streamTitleHash != hash){
            m_streamTitleHash = hash;
            if(audio_info) audio_info(sTit);
            uint8_t pos = 12;                                                   // remove "StreamTitle="
            if(sTit[pos] == '\'') pos++;                                        // remove leading  \'
            if(sTit[strlen(sTit) - 1] == '\'') sTit[strlen(sTit) -1] = '\0';    // remove trailing \'
            if(audio_showstreamtitle) audio_showstreamtitle(sTit + pos);
        }
        free(sTit);
    }

    idx1 = indexOf(ml, "StreamUrl=", 0);
    idx2 = indexOf(ml, ";", idx1);
    if(idx1 >= 0 && idx2 > idx1){                                               // StreamURL found
        uint16_t len = idx2 - idx1;
        char *sUrl;
        sUrl = strndup(ml + idx1, len + 1); sUrl[len] = '\0';

        while(i < strlen(sUrl)){hash += sUrl[i] * i+1; i++;}
        if(m_streamTitleHash != hash){
            m_streamTitleHash = hash;
            if(audio_info) audio_info(sUrl);
        }
        free(sUrl);
    }

    idx1 = indexOf(ml, "adw_ad=", 0);
    if(idx1 >= 0){                                                              // Advertisement found
        idx1 = indexOf(ml, "durationMilliseconds=", 0);
        idx2 = indexOf(ml, ";", idx1);
        if(idx1 >= 0 && idx2 > idx1){
            uint16_t len = idx2 - idx1;
            char *sAdv;
            sAdv = strndup(ml + idx1, len + 1); sAdv[len] = '\0';
            if(audio_info) audio_info(sAdv);
            uint8_t pos = 21;                                                   // remove "StreamTitle="
            if(sAdv[pos] == '\'') pos++;                                        // remove leading  \'
            if(sAdv[strlen(sAdv) - 1] == '\'') sAdv[strlen(sAdv) -1] = '\0';    // remove trailing \'
            if(audio_commercial) audio_commercial(sAdv + pos);
            free(sAdv);
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::showCodecParams(){
    // print Codec Parameter (mp3, aac) in audio_info()
    if(audio_info) {
        sprintf(chbuf,"Channels: %i", getChannels());
        audio_info(chbuf);
        sprintf(chbuf,"SampleRate: %i", getSampleRate());
        audio_info(chbuf);
        sprintf(chbuf,"BitsPerSample: %i", getBitsPerSample());
        audio_info(chbuf);
        if(getBitRate()){
            sprintf(chbuf,"BitRate: %i", getBitRate());
            audio_info(chbuf);
        }
        else {
            audio_info("BitRate: N/A");
        }
    }
}
//---------------------------------------------------------------------------------------------------------------------
int Audio::findNextSync(uint8_t* data, size_t len){
    // Mp3 and aac audio data are divided into frames. At the beginning of each frame there is a sync word.
    // The sync word is 0xFFF. This is followed by information about the structure of the frame.
    // Wav files have no frames
    // Return: 0 the synchronous word was found at position 0
    //         > 0 is the offset to the next sync word
    //         -1 the sync word was not found within the block with the length len

    int nextSync;
    static uint32_t swnf = 0;
    if(m_codec == CODEC_MP3) {
        nextSync = MP3FindSyncWord(data, len);
    }
    if(nextSync == -1) {
         if(audio_info && swnf == 0) audio_info("syncword not found");
         if(m_codec == CODEC_OGG_FLAC){
             nextSync = len;
         }
         else {
             swnf++; // syncword not found counter, can be multimediadata
         }
     }
     if (nextSync == 0){
         if(audio_info && swnf>0){
             sprintf(chbuf, "syncword not found %i times", swnf);
             audio_info(chbuf);
             swnf = 0;
         }
         else {
             if(audio_info) audio_info("syncword found at pos 0");
         }
     }
     if(nextSync > 0){
         AUDIO_INFO(sprintf(chbuf, "syncword found at pos %i", nextSync);)
     }
     return nextSync;
}
//---------------------------------------------------------------------------------------------------------------------
int Audio::sendBytes(uint8_t* data, size_t len) {
    int bytesLeft;
    static bool f_setDecodeParamsOnce = true;
    int nextSync = 0;
    if(!m_f_playing) {
        f_setDecodeParamsOnce = true;
        nextSync = findNextSync(data, len);
        if(nextSync == 0) { m_f_playing = true;}
        return nextSync;
    }
    // m_f_playing is true at this pos
    bytesLeft = len;
    int ret = 0;
    int bytesDecoded = 0;
    if(m_codec == CODEC_WAV){ //copy len data in outbuff and set validsamples and bytesdecoded=len
        memmove(m_outBuff, data , len);
        if(getBitsPerSample() == 16) m_validSamples = len / (2 * getChannels());
        if(getBitsPerSample() == 8 ) m_validSamples = len / 2;
        bytesLeft = 0;
    }
    if(m_codec == CODEC_MP3)      ret = MP3Decode(data, &bytesLeft, m_outBuff, 0);

    bytesDecoded = len - bytesLeft;
    if(bytesDecoded == 0 && ret == 0){ // unlikely framesize
            if(audio_info) audio_info("framesize is 0, start decoding again");
            m_f_playing = false; // seek for new syncword
        // we're here because there was a wrong sync word
        // so skip two sync bytes and seek for next
        return 1;
    }
    if(ret < 0) { // Error, skip the frame...
        //if(m_codec == CODEC_M4A){log_i("begin not found"); return 1;}
        i2s_zero_dma_buffer((i2s_port_t)m_i2s_num);
        if(!getChannels() && (ret == -2)) {
             ; // suppress errorcode MAINDATA_UNDERFLOW
        }
        else {
            printDecodeError(ret);
            m_f_playing = false; // seek for new syncword
        }
        if(!bytesDecoded) bytesDecoded = 2;
        return bytesDecoded;
    }
    else{  // ret>=0
        if(f_setDecodeParamsOnce){
            f_setDecodeParamsOnce = false;
            m_PlayingStartTime = millis();

            if(m_codec == CODEC_MP3){
                setChannels(MP3GetChannels());
                setSampleRate(MP3GetSampRate());
                setBitsPerSample(MP3GetBitsPerSample());
                setBitrate(MP3GetBitrate());
            }
            showCodecParams();
        }
        if(m_codec == CODEC_MP3){
            m_validSamples = MP3GetOutputSamps() / getChannels();
        }
    }
    compute_audioCurrentTime(bytesDecoded);

    if(audio_process_extern){
        bool continueI2S = false;
        audio_process_extern(m_outBuff, m_validSamples, &continueI2S);
        if(!continueI2S){
            return bytesDecoded;
        }
    }

    while(m_validSamples) {
        playChunk();
    }
    return bytesDecoded;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::compute_audioCurrentTime(int bd) {
    static uint16_t loop_counter = 0;
    static int old_bitrate = 0;
    static uint64_t sum_bitrate = 0;
    static boolean f_CBR = true; // constant bitrate

    if(m_codec == CODEC_MP3) {setBitrate(MP3GetBitrate()) ;} // if not CBR, bitrate can be changed
    if(!getBitRate()) return;

    //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_avr_bitrate == 0) { // first time
        loop_counter = 0;
        old_bitrate = 0;
        sum_bitrate = 0;
        f_CBR = true;
        m_avr_bitrate = getBitRate();
        old_bitrate = getBitRate();
    }
    if(!m_avr_bitrate) return;
    //- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    if(loop_counter < 1000) loop_counter ++;

    if((old_bitrate != getBitRate()) && f_CBR) {
        if(audio_info) audio_info("VBR recognized, audioFileDuration is estimated");
        f_CBR = false; // variable bitrate
    }
    old_bitrate = getBitRate();

    if(!f_CBR) {
        if(loop_counter > 20 && loop_counter < 200) {
            // if VBR: m_avr_bitrate is average of the first values of m_bitrate
            sum_bitrate += getBitRate();
            m_avr_bitrate = sum_bitrate / (loop_counter - 20);
            if(loop_counter == 199 && m_resumeFilePos){
                m_audioCurrentTime = ((getFilePos() - m_audioDataStart - inBufferFilled()) / m_avr_bitrate) * 8; // #293
            }
        }
    }
    else {
        if(loop_counter == 2){
            m_avr_bitrate = getBitRate();
            if(m_resumeFilePos){  // if connecttoFS() is called with resumeFilePos != 0
                m_audioCurrentTime = ((getFilePos() - m_audioDataStart - inBufferFilled()) / m_avr_bitrate) * 8; // #293
            }
        }
    }
    m_audioCurrentTime += ((float)bd / m_avr_bitrate) * 8;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::printDecodeError(int r) {
    const char *e;

    if(m_codec == CODEC_MP3){
        switch(r){
            case ERR_MP3_NONE:                              e = "NONE";                             break;
            case ERR_MP3_INDATA_UNDERFLOW:                  e = "INDATA_UNDERFLOW";                 break;
            case ERR_MP3_MAINDATA_UNDERFLOW:                e = "MAINDATA_UNDERFLOW";               break;
            case ERR_MP3_FREE_BITRATE_SYNC:                 e = "FREE_BITRATE_SYNC";                break;
            case ERR_MP3_OUT_OF_MEMORY:                     e = "OUT_OF_MEMORY";                    break;
            case ERR_MP3_NULL_POINTER:                      e = "NULL_POINTER";                     break;
            case ERR_MP3_INVALID_FRAMEHEADER:               e = "INVALID_FRAMEHEADER";              break;
            case ERR_MP3_INVALID_SIDEINFO:                  e = "INVALID_SIDEINFO";                 break;
            case ERR_MP3_INVALID_SCALEFACT:                 e = "INVALID_SCALEFACT";                break;
            case ERR_MP3_INVALID_HUFFCODES:                 e = "INVALID_HUFFCODES";                break;
            case ERR_MP3_INVALID_DEQUANTIZE:                e = "INVALID_DEQUANTIZE";               break;
            case ERR_MP3_INVALID_IMDCT:                     e = "INVALID_IMDCT";                    break;
            case ERR_MP3_INVALID_SUBBAND:                   e = "INVALID_SUBBAND";                  break;
            default: e = "ERR_UNKNOWN";
        }
        AUDIO_INFO(sprintf(chbuf, "MP3 decode error %d : %s", r, e);)
     }
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setPinout(uint8_t BCLK, uint8_t LRC, uint8_t DOUT, int8_t DIN) {

    m_pin_config.bck_io_num   = BCLK;
    m_pin_config.ws_io_num    = LRC; //  wclk
    m_pin_config.data_out_num = DOUT;
    m_pin_config.data_in_num  = DIN;

    const esp_err_t result = i2s_set_pin((i2s_port_t) m_i2s_num, &m_pin_config);
    return (result == ESP_OK);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::getFileSize() {
#ifdef AUDIO_NO_SD_FS
    return 0;
#else
    if(!audiofile) return 0;
    return audiofile.size();
#endif // AUDIO_NO_SD_FS
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::getFilePos() {
#ifdef AUDIO_NO_SD_FS
    return 0;
#else
    if(!audiofile) return 0;
    return audiofile.position();
#endif // AUDIO_NO_SD_FS
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::getAudioDataStartPos() {
#ifdef AUDIO_NO_SD_FS
    return 0;
#else
    if(!audiofile) return 0;
    return m_audioDataStart;
#endif // AUDIO_NO_SD_FS
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::getAudioFileDuration() {
#ifndef AUDIO_NO_SD_FS
    if(m_f_localfile) {if(!audiofile) return 0;}
#endif
    if(m_f_webfile)   {if(!m_contentlength) return 0;}

    if     (m_avr_bitrate && m_codec == CODEC_MP3)   m_audioFileDuration = 8 * (m_audioDataSize / m_avr_bitrate); // #289
    else return 0;
    return m_audioFileDuration;
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::getAudioCurrentTime() {  // return current time in seconds
    return (uint32_t) m_audioCurrentTime;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setAudioPlayPosition(uint16_t sec){
    // Jump to an absolute position in time within an audio file
    // e.g. setAudioPlayPosition(300) sets the pointer at pos 5 min
    // works only with format mp3 or wav
    if(m_codec == CODEC_M4A)  return false;
    if(sec > getAudioFileDuration()) sec = getAudioFileDuration();
    uint32_t filepos = m_audioDataStart + (m_avr_bitrate * sec / 8);

    return setFilePos(filepos);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::getTotalPlayingTime() {
    // Is set to zero by a connectToXXX() and starts as soon as the first audio data is available,
    // the time counting is not interrupted by a 'pause / resume' and is not reset by a fileloop
    return millis() - m_PlayingStartTime;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setTimeOffset(int sec){
    // fast forward or rewind the current position in seconds
    // audiosource must be a mp3, aac or wav file

#ifndef AUDIO_NO_SD_FS

    if(!audiofile || !m_avr_bitrate) return false;

    uint32_t oneSec  = m_avr_bitrate / 8;                   // bytes decoded in one sec
    int32_t  offset  = oneSec * sec;                        // bytes to be wind/rewind
    uint32_t startAB = m_audioDataStart;                    // audioblock begin
    uint32_t endAB   = m_audioDataStart + m_audioDataSize;  // audioblock end

    if(m_codec == CODEC_MP3 || m_codec == CODEC_AAC || m_codec == CODEC_WAV || m_codec == CODEC_FLAC){
        int32_t pos = getFilePos();
        pos += offset;
        if(pos <  (int32_t)startAB) pos = startAB;
        if(pos >= (int32_t)endAB)   pos = endAB;
        setFilePos(pos);
        return true;
    }
#endif // AUDIO_NO_SD_FS
    return false;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setFilePos(uint32_t pos) {
#ifdef AUDIO_NO_SD_FS
    return false;
#else
    if(!audiofile) return false;
//    if(!m_avr_bitrate) return false;
    if(m_codec == CODEC_M4A) return false;
    m_f_playing = false;
    if(m_codec == CODEC_MP3) MP3Decoder_ClearBuffer();
    InBuff.resetBuffer();
    if(pos < m_audioDataStart) pos = m_audioDataStart; // issue #96
    if(m_avr_bitrate) m_audioCurrentTime = ((pos-m_audioDataStart) / m_avr_bitrate) * 8; // #96
    return audiofile.seek(pos);
#endif // AUDIO_NO_SD_FS
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::audioFileSeek(const float speed) {
    // 0.5 is half speed
    // 1.0 is normal speed
    // 1.5 is one and half speed
    if((speed > 1.5f) || (speed < 0.25f)) return false;

    uint32_t srate = getSampleRate() * speed;
    i2s_set_sample_rates((i2s_port_t)m_i2s_num, srate);
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setSampleRate(uint32_t sampRate) {
    if(!sampRate) sampRate = 16000; // fuse, if there is no value -> set default #209
    i2s_set_sample_rates((i2s_port_t)m_i2s_num, sampRate);
    m_sampleRate = sampRate;
    IIR_calculateCoefficients(m_gain0, m_gain1, m_gain2); // must be recalculated after each samplerate change
    return true;
}
uint32_t Audio::getSampleRate(){
    return m_sampleRate;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setBitsPerSample(int bits) {
    if((bits != 16) && (bits != 8)) return false;
    m_bitsPerSample = bits;
    return true;
}
uint8_t Audio::getBitsPerSample(){
    return m_bitsPerSample;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setChannels(int ch) {
    if((ch < 1) || (ch > 2)) return false;
    m_channels = ch;
    return true;
}
uint8_t Audio::getChannels(){
    if (m_channels == 0) {    // this should not happen! #209
        m_channels = 2;
    }
    return m_channels;
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::setBitrate(int br){
    m_bitRate = br;
    if(br)return true;
    return false;
}
uint32_t Audio::getBitRate(bool avg){
    if (avg)
        return m_avr_bitrate;
    return m_bitRate;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setI2SCommFMT_LSB(bool commFMT) {
    // false: I2S communication format is by default I2S_COMM_FORMAT_I2S_MSB, right->left (AC101, PCM5102A)
    // true:  changed to I2S_COMM_FORMAT_I2S_LSB for some DACs (PT8211)
    //        Japanese or called LSBJ (Least Significant Bit Justified) format

    if (commFMT) {
        log_i("commFMT LSB");

        #if ESP_ARDUINO_VERSION_MAJOR >= 2
            m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_MSB); // v >= 2.0.0
        #else
            m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_LSB);
        #endif

    }
    else {
        log_i("commFMT MSB");

        #if ESP_ARDUINO_VERSION_MAJOR >= 2
            m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S); // vers >= 2.0.0
        #else
            m_i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
        #endif

    }
    log_i("commFMT = %i", m_i2s_config.communication_format);
    i2s_driver_uninstall((i2s_port_t)m_i2s_num);
    i2s_driver_install  ((i2s_port_t)m_i2s_num, &m_i2s_config, 0, NULL);
}
//---------------------------------------------------------------------------------------------------------------------
bool Audio::playSample(int16_t sample[2]) {

    if (getBitsPerSample() == 8) { // Upsample from unsigned 8 bits to signed 16 bits
        sample[LEFTCHANNEL]  = ((sample[LEFTCHANNEL]  & 0xff) -128) << 8;
        sample[RIGHTCHANNEL] = ((sample[RIGHTCHANNEL] & 0xff) -128) << 8;
    }

    sample[LEFTCHANNEL]  = sample[LEFTCHANNEL]  >> 1; // half Vin so we can boost up to 6dB in filters
    sample[RIGHTCHANNEL] = sample[RIGHTCHANNEL] >> 1;

    // Filterchain, can commented out if not used
    sample = IIR_filterChain0(sample);
    sample = IIR_filterChain1(sample);
    sample = IIR_filterChain2(sample);
    //-------------------------------------------

    uint32_t s32 = Gain(sample); // vosample2lume;
    
    if(m_f_internalDAC) {
        s32 += 0x80008000;
    }

    _lastGain = s32;
    
    esp_err_t err = i2s_write((i2s_port_t) m_i2s_num, (const char*) &s32, sizeof(uint32_t), &m_i2s_bytesWritten, 1000);
    if(err != ESP_OK) {
        log_e("ESP32 Errorcode %i", err);
        return false;
    }
    if(m_i2s_bytesWritten < 4) {
        log_e("Can't stuff any more in I2S..."); // increase waitingtime or outputbuffer
        return false;
    }
    return true;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setTone(int8_t gainLowPass, int8_t gainBandPass, int8_t gainHighPass){
    // see https://www.earlevel.com/main/2013/10/13/biquad-calculator-v2/
    // values can be between -40 ... +6 (dB)

    m_gain0 = gainLowPass;
    m_gain1 = gainBandPass;
    m_gain2 = gainHighPass;

    IIR_calculateCoefficients(m_gain0, m_gain1, m_gain2);

    /*
        This will cause a clicking sound when adjusting the EQ.
        Because when the EQ is adjusted, the IIR filter will be cleared and played,
        mixed in the audio data frame, and a click-like sound will be produced.
    */
    /*
    int16_t tmp[2]; tmp[0] = 0; tmp[1]= 0;

    IIR_filterChain0(tmp, true ); // flush the filter
    IIR_filterChain1(tmp, true ); // flush the filter
    IIR_filterChain2(tmp, true ); // flush the filter
    */
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::forceMono(bool m) { // #100 mono option
    m_f_forceMono = m; // false stereo, true mono
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setBalance(int8_t bal){ // bal -16...16
    if(bal < -16) bal = -16;
    if(bal >  16) bal =  16;
    m_balance = bal;
}
//---------------------------------------------------------------------------------------------------------------------
void Audio::setVolume(uint8_t vol) { // vol 22 steps, 0...21
    if(vol > 21) vol = 21;
    m_vol = volumetable[vol];
}
//---------------------------------------------------------------------------------------------------------------------
uint8_t Audio::getVolume() {
    for(uint8_t i = 0; i < 22; i++) {
        if(volumetable[i] == m_vol) return i;
    }
    m_vol = 12; // if m_vol not found in table
    return m_vol;
}
//---------------------------------------------------------------------------------------------------------------------
uint8_t Audio::getI2sPort() {
    return m_i2s_num;
}
//---------------------------------------------------------------------------------------------------------------------
int32_t Audio::Gain(int16_t s[2]) {
    int32_t v[2];
    float step = (float)m_vol /64;
    uint8_t l = 0, r = 0;

    if(m_balance < 0){
        step = step * (float)(abs(m_balance) * 4);
        l = (uint8_t)(step);
    }
    if(m_balance > 0){
        step = step * m_balance * 4;
        r = (uint8_t)(step);
    }

    v[LEFTCHANNEL] = (s[LEFTCHANNEL]  * (m_vol - l)) >> 6;
    v[RIGHTCHANNEL]= (s[RIGHTCHANNEL] * (m_vol - r)) >> 6;

    return (v[LEFTCHANNEL] << 16) | (v[RIGHTCHANNEL] & 0xffff);
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::inBufferFilled() {
    // current audio input buffer fillsize in bytes
    return InBuff.bufferFilled();
}
//---------------------------------------------------------------------------------------------------------------------
uint32_t Audio::inBufferFree() {
    // current audio input buffer free space in bytes
    return InBuff.freeSpace();
}
//---------------------------------------------------------------------------------------------------------------------
//            ***     D i g i t a l   b i q u a d r a t i c     f i l t e r     ***
//---------------------------------------------------------------------------------------------------------------------
void Audio::IIR_calculateCoefficients(int8_t G0, int8_t G1, int8_t G2){  // Infinite Impulse Response (IIR) filters

    // G1 - gain low shelf   set between -40 ... +6 dB
    // G2 - gain peakEQ      set between -40 ... +6 dB
    // G3 - gain high shelf  set between -40 ... +6 dB
    // https://www.earlevel.com/main/2012/11/26/biquad-c-source-code/

    if(getSampleRate() < 1000) return;  // fuse

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    if(G0 < -40) G0 = -40;      // -40dB -> Vin*0.01
    if(G0 > 6) G0 = 6;          // +6dB -> Vin*2
    if(G1 < -40) G1 = -40;
    if(G1 > 6) G1 = 6;
    if(G2 < -40) G2 = -40;
    if(G2 > 6) G2 = 6;

    const float FcLS   =  500;  // Frequency LowShelf[Hz]
    const float FcPKEQ = 3000;  // Frequency PeakEQ[Hz]
    const float FcHS   = 6000;  // Frequency HighShelf[Hz]

    float K, norm, Q, Fc, V ;

    // LOWSHELF
    Fc = (float)FcLS / (float)getSampleRate(); // Cutoff frequency
    K = tanf((float)PI * Fc);
    V = powf(10, fabs(G0) / 20.0);

    if (G0 >= 0) {  // boost
        norm = 1 / (1 + sqrtf(2) * K + K * K);
        m_filter[LOWSHELF].a0 = (1 + sqrtf(2*V) * K + V * K * K) * norm;
        m_filter[LOWSHELF].a1 = 2 * (V * K * K - 1) * norm;
        m_filter[LOWSHELF].a2 = (1 - sqrtf(2*V) * K + V * K * K) * norm;
        m_filter[LOWSHELF].b1 = 2 * (K * K - 1) * norm;
        m_filter[LOWSHELF].b2 = (1 - sqrtf(2) * K + K * K) * norm;
    }
    else {          // cut
        norm = 1 / (1 + sqrtf(2*V) * K + V * K * K);
        m_filter[LOWSHELF].a0 = (1 + sqrtf(2) * K + K * K) * norm;
        m_filter[LOWSHELF].a1 = 2 * (K * K - 1) * norm;
        m_filter[LOWSHELF].a2 = (1 - sqrtf(2) * K + K * K) * norm;
        m_filter[LOWSHELF].b1 = 2 * (V * K * K - 1) * norm;
        m_filter[LOWSHELF].b2 = (1 - sqrtf(2*V) * K + V * K * K) * norm;
    }

    // PEAK EQ
    Fc = (float)FcPKEQ / (float)getSampleRate(); // Cutoff frequency
    K = tanf((float)PI * Fc);
    V = powf(10, fabs(G1) / 20.0);
    Q = 2.5; // Quality factor
    if (G1 >= 0) { // boost
        norm = 1 / (1 + 1/Q * K + K * K);
        m_filter[PEAKEQ].a0 = (1 + V/Q * K + K * K) * norm;
        m_filter[PEAKEQ].a1 = 2 * (K * K - 1) * norm;
        m_filter[PEAKEQ].a2 = (1 - V/Q * K + K * K) * norm;
        m_filter[PEAKEQ].b1 = m_filter[PEAKEQ].a1;
        m_filter[PEAKEQ].b2 = (1 - 1/Q * K + K * K) * norm;
    }
    else {    // cut
        norm = 1 / (1 + V/Q * K + K * K);
        m_filter[PEAKEQ].a0 = (1 + 1/Q * K + K * K) * norm;
        m_filter[PEAKEQ].a1 = 2 * (K * K - 1) * norm;
        m_filter[PEAKEQ].a2 = (1 - 1/Q * K + K * K) * norm;
        m_filter[PEAKEQ].b1 = m_filter[PEAKEQ].a1;
        m_filter[PEAKEQ].b2 = (1 - V/Q * K + K * K) * norm;
    }

    // HIGHSHELF
    Fc = (float)FcHS / (float)getSampleRate(); // Cutoff frequency
    K = tanf((float)PI * Fc);
    V = powf(10, fabs(G2) / 20.0);
    if (G2 >= 0) {  // boost
        norm = 1 / (1 + sqrtf(2) * K + K * K);
        m_filter[HIFGSHELF].a0 = (V + sqrtf(2*V) * K + K * K) * norm;
        m_filter[HIFGSHELF].a1 = 2 * (K * K - V) * norm;
        m_filter[HIFGSHELF].a2 = (V - sqrtf(2*V) * K + K * K) * norm;
        m_filter[HIFGSHELF].b1 = 2 * (K * K - 1) * norm;
        m_filter[HIFGSHELF].b2 = (1 - sqrtf(2) * K + K * K) * norm;
    }
    else {
        norm = 1 / (V + sqrtf(2*V) * K + K * K);
        m_filter[HIFGSHELF].a0 = (1 + sqrtf(2) * K + K * K) * norm;
        m_filter[HIFGSHELF].a1 = 2 * (K * K - 1) * norm;
        m_filter[HIFGSHELF].a2 = (1 - sqrtf(2) * K + K * K) * norm;
        m_filter[HIFGSHELF].b1 = 2 * (K * K - V) * norm;
        m_filter[HIFGSHELF].b2 = (V - sqrtf(2*V) * K + K * K) * norm;
    }

//    log_i("LS a0=%f, a1=%f, a2=%f, b1=%f, b2=%f", m_filter[0].a0, m_filter[0].a1, m_filter[0].a2,
//                                                  m_filter[0].b1, m_filter[0].b2);
//    log_i("EQ a0=%f, a1=%f, a2=%f, b1=%f, b2=%f", m_filter[1].a0, m_filter[1].a1, m_filter[1].a2,
//                                                  m_filter[1].b1, m_filter[1].b2);
//    log_i("HS a0=%f, a1=%f, a2=%f, b1=%f, b2=%f", m_filter[2].a0, m_filter[2].a1, m_filter[2].a2,
//                                                  m_filter[2].b1, m_filter[2].b2);
}
//---------------------------------------------------------------------------------------------------------------------
int16_t* Audio::IIR_filterChain0(int16_t iir_in[2], bool clear){  // Infinite Impulse Response (IIR) filters

    uint8_t z1 = 0, z2 = 1;
    enum: uint8_t {in = 0, out = 1};
    float inSample[2];
    float outSample[2];
    static int16_t iir_out[2];

    if(clear){
        memset(m_filterBuff, 0, sizeof(m_filterBuff));            // zero IIR filterbuffer
        iir_out[0] = 0;
        iir_out[1] = 0;
        iir_in[0]  = 0;
        iir_in[1]  = 0;
    }

    inSample[LEFTCHANNEL]  = (float)(iir_in[LEFTCHANNEL]);
    inSample[RIGHTCHANNEL] = (float)(iir_in[RIGHTCHANNEL]);

    outSample[LEFTCHANNEL] =   m_filter[0].a0  * inSample[LEFTCHANNEL]
                             + m_filter[0].a1  * m_filterBuff[0][z1][in] [LEFTCHANNEL]
                             + m_filter[0].a2  * m_filterBuff[0][z2][in] [LEFTCHANNEL]
                             - m_filter[0].b1  * m_filterBuff[0][z1][out][LEFTCHANNEL]
                             - m_filter[0].b2  * m_filterBuff[0][z2][out][LEFTCHANNEL];

    m_filterBuff[0][z2][in] [LEFTCHANNEL]  = m_filterBuff[0][z1][in][LEFTCHANNEL];
    m_filterBuff[0][z1][in] [LEFTCHANNEL]  = inSample[LEFTCHANNEL];
    m_filterBuff[0][z2][out][LEFTCHANNEL]  = m_filterBuff[0][z1][out][LEFTCHANNEL];
    m_filterBuff[0][z1][out][LEFTCHANNEL]  = outSample[LEFTCHANNEL];
    iir_out[LEFTCHANNEL] = (int16_t)outSample[LEFTCHANNEL];


    outSample[RIGHTCHANNEL] =  m_filter[0].a0 * inSample[RIGHTCHANNEL]
                             + m_filter[0].a1 * m_filterBuff[0][z1][in] [RIGHTCHANNEL]
                             + m_filter[0].a2 * m_filterBuff[0][z2][in] [RIGHTCHANNEL]
                             - m_filter[0].b1 * m_filterBuff[0][z1][out][RIGHTCHANNEL]
                             - m_filter[0].b2 * m_filterBuff[0][z2][out][RIGHTCHANNEL];

    m_filterBuff[0][z2][in] [RIGHTCHANNEL] = m_filterBuff[0][z1][in][RIGHTCHANNEL];
    m_filterBuff[0][z1][in] [RIGHTCHANNEL] = inSample[RIGHTCHANNEL];
    m_filterBuff[0][z2][out][RIGHTCHANNEL] = m_filterBuff[0][z1][out][RIGHTCHANNEL];
    m_filterBuff[0][z1][out][RIGHTCHANNEL] = outSample[RIGHTCHANNEL];
    iir_out[RIGHTCHANNEL] = (int16_t) outSample[RIGHTCHANNEL];

    return iir_out;
}
//---------------------------------------------------------------------------------------------------------------------
int16_t* Audio::IIR_filterChain1(int16_t iir_in[2], bool clear){  // Infinite Impulse Response (IIR) filters

    uint8_t z1 = 0, z2 = 1;
    enum: uint8_t {in = 0, out = 1};
    float inSample[2];
    float outSample[2];
    static int16_t iir_out[2];

    if(clear){
        memset(m_filterBuff, 0, sizeof(m_filterBuff));            // zero IIR filterbuffer
        iir_out[0] = 0;
        iir_out[1] = 0;
        iir_in[0]  = 0;
        iir_in[1]  = 0;
    }

    inSample[LEFTCHANNEL]  = (float)(iir_in[LEFTCHANNEL]);
    inSample[RIGHTCHANNEL] = (float)(iir_in[RIGHTCHANNEL]);

    outSample[LEFTCHANNEL] =   m_filter[1].a0  * inSample[LEFTCHANNEL]
                             + m_filter[1].a1  * m_filterBuff[1][z1][in] [LEFTCHANNEL]
                             + m_filter[1].a2  * m_filterBuff[1][z2][in] [LEFTCHANNEL]
                             - m_filter[1].b1  * m_filterBuff[1][z1][out][LEFTCHANNEL]
                             - m_filter[1].b2  * m_filterBuff[1][z2][out][LEFTCHANNEL];

    m_filterBuff[1][z2][in] [LEFTCHANNEL]  = m_filterBuff[1][z1][in][LEFTCHANNEL];
    m_filterBuff[1][z1][in] [LEFTCHANNEL]  = inSample[LEFTCHANNEL];
    m_filterBuff[1][z2][out][LEFTCHANNEL]  = m_filterBuff[1][z1][out][LEFTCHANNEL];
    m_filterBuff[1][z1][out][LEFTCHANNEL]  = outSample[LEFTCHANNEL];
    iir_out[LEFTCHANNEL] = (int16_t)outSample[LEFTCHANNEL];


    outSample[RIGHTCHANNEL] =  m_filter[1].a0 * inSample[RIGHTCHANNEL]
                             + m_filter[1].a1 * m_filterBuff[1][z1][in] [RIGHTCHANNEL]
                             + m_filter[1].a2 * m_filterBuff[1][z2][in] [RIGHTCHANNEL]
                             - m_filter[1].b1 * m_filterBuff[1][z1][out][RIGHTCHANNEL]
                             - m_filter[1].b2 * m_filterBuff[1][z2][out][RIGHTCHANNEL];

    m_filterBuff[1][z2][in] [RIGHTCHANNEL] = m_filterBuff[1][z1][in][RIGHTCHANNEL];
    m_filterBuff[1][z1][in] [RIGHTCHANNEL] = inSample[RIGHTCHANNEL];
    m_filterBuff[1][z2][out][RIGHTCHANNEL] = m_filterBuff[1][z1][out][RIGHTCHANNEL];
    m_filterBuff[1][z1][out][RIGHTCHANNEL] = outSample[RIGHTCHANNEL];
    iir_out[RIGHTCHANNEL] = (int16_t) outSample[RIGHTCHANNEL];

    return iir_out;
}
//---------------------------------------------------------------------------------------------------------------------
int16_t* Audio::IIR_filterChain2(int16_t iir_in[2], bool clear){  // Infinite Impulse Response (IIR) filters

    uint8_t z1 = 0, z2 = 1;
    enum: uint8_t {in = 0, out = 1};
    float inSample[2];
    float outSample[2];
    static int16_t iir_out[2];

    if(clear){
        memset(m_filterBuff, 0, sizeof(m_filterBuff));            // zero IIR filterbuffer
        iir_out[0] = 0;
        iir_out[1] = 0;
        iir_in[0]  = 0;
        iir_in[1]  = 0;
    }

    inSample[LEFTCHANNEL]  = (float)(iir_in[LEFTCHANNEL]);
    inSample[RIGHTCHANNEL] = (float)(iir_in[RIGHTCHANNEL]);

    outSample[LEFTCHANNEL] =   m_filter[2].a0  * inSample[LEFTCHANNEL]
                             + m_filter[2].a1  * m_filterBuff[2][z1][in] [LEFTCHANNEL]
                             + m_filter[2].a2  * m_filterBuff[2][z2][in] [LEFTCHANNEL]
                             - m_filter[2].b1  * m_filterBuff[2][z1][out][LEFTCHANNEL]
                             - m_filter[2].b2  * m_filterBuff[2][z2][out][LEFTCHANNEL];

    m_filterBuff[2][z2][in] [LEFTCHANNEL]  = m_filterBuff[2][z1][in][LEFTCHANNEL];
    m_filterBuff[2][z1][in] [LEFTCHANNEL]  = inSample[LEFTCHANNEL];
    m_filterBuff[2][z2][out][LEFTCHANNEL]  = m_filterBuff[2][z1][out][LEFTCHANNEL];
    m_filterBuff[2][z1][out][LEFTCHANNEL]  = outSample[LEFTCHANNEL];
    iir_out[LEFTCHANNEL] = (int16_t)outSample[LEFTCHANNEL];


    outSample[RIGHTCHANNEL] =  m_filter[2].a0 * inSample[RIGHTCHANNEL]
                             + m_filter[2].a1 * m_filterBuff[2][z1][in] [RIGHTCHANNEL]
                             + m_filter[2].a2 * m_filterBuff[2][z2][in] [RIGHTCHANNEL]
                             - m_filter[2].b1 * m_filterBuff[2][z1][out][RIGHTCHANNEL]
                             - m_filter[2].b2 * m_filterBuff[2][z2][out][RIGHTCHANNEL];

    m_filterBuff[2][z2][in] [RIGHTCHANNEL] = m_filterBuff[2][z1][in][RIGHTCHANNEL];
    m_filterBuff[2][z1][in] [RIGHTCHANNEL] = inSample[RIGHTCHANNEL];
    m_filterBuff[2][z2][out][RIGHTCHANNEL] = m_filterBuff[2][z1][out][RIGHTCHANNEL];
    m_filterBuff[2][z1][out][RIGHTCHANNEL] = outSample[RIGHTCHANNEL];
    iir_out[RIGHTCHANNEL] = (int16_t) outSample[RIGHTCHANNEL];

    return iir_out;
}

uint32_t Audio::getLastGain(){
    return _lastGain;
}
