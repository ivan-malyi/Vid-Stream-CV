// сам Gstreamer 
#include <gst/gst.h>
// позволяет получать медиаданные в коде приложения
#include <gst/app/gstappsink.h> 
#include <iostream> 
#include <string>

/* 
            Обработчик сообщений из шины GStreamer
1. GstBus - шина сообщений для конвейера GStreamer
2. GstMessage - объект сообщения, которое нужно обработать
3. gpointer data - указатель на данные пользователя, в данном случае главный цикл (GMainLoop)
        
*/
static gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    GMainLoop *loop = (GMainLoop*)data;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            std::cerr << "Error: " << err->message << std::endl;
            g_error_free(err);
            g_free(debug);
            g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_EOS:
            std::cout << "End of stream" << std::endl;
            g_main_loop_quit(loop);
            break;
        default:
            break;
    }
    return TRUE;
}

// Обработчик новых сэмплов (кадров) из appsink
// Эта функция вызывается каждый раз, когда appsink получает новый кадр
// В функции используется maping от Gstreamer, который предосталвяет безопасный доступ к буферу: блокирует доступ к буферу на момент записи 
// map.data: Указатель на данные буфера (в нашем случае — кадр видео).
// map.size: Размер данных в байтах.
static GstFlowReturn new_sample_callback(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo map;
    
    // Получаем сэмпл из appsink
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink)); //  извлекает новый сэмпл (кадр) из appsink
    if (sample) {
        buffer = gst_sample_get_buffer(sample); // Получаем буфер из сэмпла
        
        // Отображаем буфер в память для чтения
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            // Здесь данные кадра доступны в map.data
            // Размер данных: map.size
            // Тут будем пользоваться OpenCV для обработки кадров с видео, передаваемое через Gstreamer 
            
            std::cout << "Received frame: " << map.size << " bytes" << std::endl;
            
            // Размапливаем буфер - обязательное дейсвие, после обработки кадра, чтобы разблокировать буфер 
            gst_buffer_unmap(buffer, &map);
        }
        
        // Освобождаем сэмпл
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    
    return GST_FLOW_ERROR;
}

int main(int argc, char *argv[]) {
    // Инициализация GStreamer
    gst_init(&argc, &argv);
    
    // Создаем главный цикл
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    
    // Создаем элементы конвейера
    GstElement *pipeline, *source, *convert, *scale, *sink;
    
    // Создаем конвейер
    pipeline = gst_pipeline_new("camera-pipeline");
    
    
    // v4l2src - источник видео для Linux (Video4Linux2)
    // Windows аналоги - ksvideosrc или dshowvideosrc
    source = gst_element_factory_make("v4l2src", "camera-source");
    convert = gst_element_factory_make("videoconvert", "converter");
    scale = gst_element_factory_make("videoscale", "scaler");
    sink = gst_element_factory_make("appsink", "video-sink");
    
    // Проверяем, что все элементы созданы успешно
    if (!pipeline || !source || !convert || !scale || !sink) {
        std::cerr << "Не удалось создать один из элементов!" << std::endl;
        return -1;
    }
    
    // Настраиваем appsink для получения кадров
    g_object_set(G_OBJECT(sink), "emit-signals", TRUE, NULL);
    g_object_set(G_OBJECT(sink), "max-buffers", 1, NULL);
    g_object_set(G_OBJECT(sink), "drop", TRUE, NULL);
    
    // Устанавливаем формат видео для appsink (RGB, 640x480)
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGB",
                                        "width", G_TYPE_INT, 640,
                                        "height", G_TYPE_INT, 480,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
    gst_app_sink_set_caps(GST_APP_SINK(sink), caps);
    gst_caps_unref(caps);
    
    // Подключаем сигнал new-sample к обработчику
    g_signal_connect(sink, "new-sample", G_CALLBACK(new_sample_callback), NULL);
    
    // Добавляем все элементы в конвейер
    gst_bin_add_many(GST_BIN(pipeline), source, convert, scale, sink, NULL);
    
    // Связываем элементы вместе
    if (!gst_element_link_many(source, convert, scale, sink, NULL)) {
        std::cerr << "Элементы не могут быть связаны!" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }
    
    // Подключаем обработчик сообщений к шине
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_callback, loop);
    gst_object_unref(bus);
    
    // Запускаем конвейер
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Не удалось запустить конвейер!" << std::endl;
        gst_object_unref(pipeline);
        return -1;
    }
    
    std::cout << "Конвейер запущен, захват видео начат..." << std::endl;
    
    // Запускаем главный цикл
    g_main_loop_run(loop);
    
    // Очистка
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    
    return 0;
}