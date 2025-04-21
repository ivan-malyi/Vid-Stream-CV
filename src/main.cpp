#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include <iostream>
#include <string>

using namespace cv;

// Структура для конвейера захвата
typedef struct _SrcData {
    GstElement* pipeline;
    GstElement* source;
    GstElement* convert;
    GstElement* scale;
    GstElement* sink;
} SrcData;

// Структура для конвейера передачи
typedef struct _DstData {
    GstElement* pipeline;
    GstElement* appsrc;
    GstElement* convert;
    GstElement* encoder;
    GstElement* payloader;
    GstElement* udpsink;
} DstData;

GMainLoop *main_loop = nullptr;

// Обработчик сообщений из шины GStreamer
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

// Функция для преобразования Mat в GstBuffer
GstBuffer *mat_to_gst_buffer(const Mat &frame) {
    gsize size = frame.step[0] * frame.rows;

    GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);

    if (!buffer) {
        std::cerr << "Couldn't create buffer!" << std::endl;
        return nullptr;
    }

    GstMapInfo map;

    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        std::cerr << "Couldn't map buffer!" << std::endl;
        return nullptr;
    }

    if (frame.isContinuous()) {
        memcpy(map.data, frame.data, size);
    } else {
        size_t row_size = frame.cols * frame.elemSize();
        for (int i = 0; i < frame.rows; ++i) {
            memcpy(map.data + i * row_size, frame.ptr(i), row_size);
        }
    }

    gst_buffer_unmap(buffer, &map);
    return buffer;
}

// Функция для преобразования Mat в GstSample
GstSample *mat_to_gst_sample(const Mat &frame, GstCaps *caps) {
    GstBuffer *buffer = mat_to_gst_buffer(frame);

    if (!buffer) {
        return nullptr;
    }

    GstClockTime timestamp = gst_util_get_timestamp();

    GST_BUFFER_PTS(buffer) = timestamp;
    GST_BUFFER_DTS(buffer) = timestamp;

    GstSample *sample = gst_sample_new(buffer, caps, NULL, NULL);

    gst_buffer_unref(buffer);

    return sample;
}

// Функция для преобразования GstSample в Mat
Mat gst_sample_to_mat(GstSample* sample) {
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        std::cerr << "Cannot map gstreamer buffer" << std::endl;
        return Mat();
    }
    
    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    
    int width, height;
    gst_structure_get_int(structure, "width", &width);
    gst_structure_get_int(structure, "height", &height);

    const char* format = gst_structure_get_string(structure, "format");
    int type;

    if (g_str_equal(format, "RGB")) {
        type = CV_8UC3;
    } else if (g_str_equal(format, "BGR")) {
        type = CV_8UC3;
    } else if (g_str_equal(format, "RGBA")) {
        type = CV_8UC4;
    } else if (g_str_equal(format, "BGRA")) {
        type = CV_8UC4;
    } else if (g_str_equal(format, "GRAY8")) {
        type = CV_8UC1;
    } else {
        type = CV_8UC3;
    }

    Mat frame(height, width, type, (void*)map.data);

    if (g_str_equal(format, "RGB")) {
        cvtColor(frame, frame, COLOR_RGB2BGR);
    }

    Mat result = frame.clone();

    gst_buffer_unmap(buffer, &map);
    
    return result;
}

// Функция для обработки кадров с помощью OpenCV
Mat process_frame(const Mat &input_frame) {
    if(input_frame.empty()) {
        std::cerr << "Error: Empty input frame" << std::endl;
        return Mat();
    }

    Mat processed_frame;
    
    // Делаем копию входного кадра для обработки
    processed_frame = input_frame.clone();
    
    // Применяем размытие по Гауссу
    GaussianBlur(processed_frame, processed_frame, Size(5, 5), 1.5);
    
    // Обнаружение краев с помощью Canny
    Mat edges;
    Canny(processed_frame, edges, 100, 200);
    
    // Поиск контуров
    std::vector<std::vector<Point>> contours;
    findContours(edges, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    
    // Рисуем контуры на исходном изображении
    drawContours(processed_frame, contours, -1, Scalar(0, 255, 0), 2);
    
    // Добавляем текст с информацией
    std::string info = "Frame contours: " + std::to_string(contours.size());
    putText(processed_frame, info, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 0, 255), 2);
    
    return processed_frame;
}

static GstFlowReturn new_sample_callback(GstElement *sink, gpointer data) {
    DstData* dst_data = (DstData*)data;
    GstSample *sample;
    
    // Get sample from appsink
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    
    if (!sample) {
        std::cerr << "Couldn't acquire sample" << std::endl;
        return GST_FLOW_ERROR;
    }
    
    // Convert sample to Mat
    Mat frame = gst_sample_to_mat(sample);
    
    if (frame.empty()) {
        std::cerr << "Empty frame!" << std::endl;
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    // Process the image
    Mat processed_frame = process_frame(frame);
    
    // Display the result
    imshow("GStreamer + OpenCV", processed_frame);
    int key = waitKey(30);
    if (key == 27 && main_loop) { // ESC key
        g_main_loop_quit(main_loop);
    }
    
    // Get caps from sample to create a new sample
    GstCaps* caps = gst_sample_get_caps(sample);
    gst_sample_unref(sample);
    
    // Create a new sample from processed image
    GstSample* out_sample = mat_to_gst_sample(processed_frame, caps);
    
    if (out_sample) {
        // Send processed frame to UDP stream
        GstFlowReturn ret = gst_app_src_push_sample(GST_APP_SRC(dst_data->appsrc), out_sample);
        gst_sample_unref(out_sample);
        
        if (ret != GST_FLOW_OK) {
            std::cerr << "Error during sending frame to appsrc: " << ret << std::endl;
            return GST_FLOW_ERROR;
        }
    }
    
    return GST_FLOW_OK;
}

int main(int argc, char *argv[]) {
    // Инициализация GStreamer
    gst_init(&argc, &argv);
    
    // Создаем главный цикл
    main_loop = g_main_loop_new(NULL, FALSE);
    
    // Создаем структуры для конвейеров
    SrcData src_data;
    DstData dst_data;
    
    // Создаем конвейер источника
    src_data.pipeline = gst_pipeline_new("src_pipeline");
    src_data.source = gst_element_factory_make("v4l2src", "src_source");
    src_data.convert = gst_element_factory_make("videoconvert", "src_convert");
    src_data.scale = gst_element_factory_make("videoscale", "src_scale");
    src_data.sink = gst_element_factory_make("appsink", "src_sink");
    
    // Создаем конвейер назначения
    dst_data.pipeline = gst_pipeline_new("dst_pipeline");
    dst_data.appsrc = gst_element_factory_make("appsrc", "dst_source");
    dst_data.convert = gst_element_factory_make("videoconvert", "dst_convert");
    dst_data.encoder = gst_element_factory_make("x264enc", "dst_encoder");
    dst_data.payloader = gst_element_factory_make("rtph264pay", "dst_payloader");
    dst_data.udpsink = gst_element_factory_make("udpsink", "dst_udpsink");
    
    // Проверяем элементы src конвейера
    if (!src_data.pipeline || !src_data.source || !src_data.convert || 
        !src_data.scale || !src_data.sink) {
        std::cerr << "Не удалось создать элементы src конвейера!" << std::endl;
        return -1;
    }
    
    // Проверяем элементы dst конвейера
    if (!dst_data.pipeline || !dst_data.appsrc || !dst_data.convert || 
        !dst_data.encoder || !dst_data.payloader || !dst_data.udpsink) {
        std::cerr << "Не удалось создать элементы dst конвейера!" << std::endl;
        return -1;
    }
    
    // Настраиваем appsink для получения кадров
    g_object_set(G_OBJECT(src_data.sink), "emit-signals", TRUE, NULL);
    g_object_set(G_OBJECT(src_data.sink), "max-buffers", 1, NULL);
    g_object_set(G_OBJECT(src_data.sink), "drop", TRUE, NULL);
    
    // Настраиваем appsrc
    g_object_set(G_OBJECT(dst_data.appsrc), "stream-type", 0, NULL); // GST_APP_STREAM_TYPE_STREAM
    g_object_set(G_OBJECT(dst_data.appsrc), "format", GST_FORMAT_TIME, NULL);
    g_object_set(G_OBJECT(dst_data.appsrc), "is-live", TRUE, NULL);
    
    // Настраиваем UDP-sink
    g_object_set(G_OBJECT(dst_data.udpsink), "host", "127.0.0.1", NULL); // Локальный адрес для тестирования
    g_object_set(G_OBJECT(dst_data.udpsink), "port", 5000, NULL);
    
    // Настраиваем H.264 кодер
    g_object_set(G_OBJECT(dst_data.encoder), "tune", 4, NULL);  // zerolatency preset
    g_object_set(G_OBJECT(dst_data.encoder), "speed-preset", 1, NULL);  // ultrafast
    g_object_set(G_OBJECT(dst_data.encoder), "bitrate", 500, NULL);  // 500 kbps
    
    // Устанавливаем формат видео
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                       "format", G_TYPE_STRING, "RGB",
                                       "width", G_TYPE_INT, 640,
                                       "height", G_TYPE_INT, 480,
                                       "framerate", GST_TYPE_FRACTION, 30, 1,
                                       NULL);
    
    // Устанавливаем caps для appsink и appsrc
    gst_app_sink_set_caps(GST_APP_SINK(src_data.sink), caps);
    g_object_set(G_OBJECT(dst_data.appsrc), "caps", caps, NULL);

    // Connect handler for new frames
    g_signal_connect(src_data.sink, "new-sample", G_CALLBACK(new_sample_callback), &dst_data);
    
    // Добавляем элементы в конвейеры
    gst_bin_add_many(GST_BIN(src_data.pipeline), src_data.source, src_data.convert, src_data.scale, src_data.sink, NULL);
    gst_bin_add_many(GST_BIN(dst_data.pipeline), dst_data.appsrc, dst_data.convert, dst_data.encoder, dst_data.payloader, dst_data.udpsink, NULL);
    
    // Связываем элементы src конвейера
    if (!gst_element_link_many(src_data.source, src_data.convert, src_data.scale, src_data.sink, NULL)) {
        std::cerr << "Элементы src конвейера не могут быть связаны!" << std::endl;
        gst_object_unref(src_data.pipeline);
        gst_object_unref(dst_data.pipeline);
        return -1;
    }
    
    // Связываем элементы dst конвейера
    if (!gst_element_link_many(dst_data.appsrc, dst_data.convert, dst_data.encoder, dst_data.payloader, dst_data.udpsink, NULL)) {
        std::cerr << "Элементы dst конвейера не могут быть связаны!" << std::endl;
        gst_object_unref(src_data.pipeline);
        gst_object_unref(dst_data.pipeline);
        return -1;
    }
    
    GstBus *src_bus = gst_element_get_bus(src_data.pipeline);
    gst_bus_add_watch(src_bus, bus_callback, main_loop);
    gst_object_unref(src_bus);
    
    GstBus *dst_bus = gst_element_get_bus(dst_data.pipeline);
    gst_bus_add_watch(dst_bus, bus_callback, main_loop);
    gst_object_unref(dst_bus);
    
    // Create OpenCV window
    namedWindow("GStreamer + OpenCV", WINDOW_AUTOSIZE);
    
    // Start pipelines
    GstStateChangeReturn src_ret = gst_element_set_state(src_data.pipeline, GST_STATE_PLAYING);
    GstStateChangeReturn dst_ret = gst_element_set_state(dst_data.pipeline, GST_STATE_PLAYING);
    
    if (src_ret == GST_STATE_CHANGE_FAILURE || dst_ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to start pipeline!" << std::endl;
        return -1;
    }
    
    std::cout << "Pipelines started, capturing video..." << std::endl;
    
    // Start main loop
    g_main_loop_run(main_loop);
    
    // Cleanup resources
    destroyAllWindows();
    gst_caps_unref(caps);
    
    gst_element_set_state(src_data.pipeline, GST_STATE_NULL);
    gst_element_set_state(dst_data.pipeline, GST_STATE_NULL);
    
    gst_object_unref(GST_OBJECT(src_data.pipeline));
    gst_object_unref(GST_OBJECT(dst_data.pipeline));
    
    g_main_loop_unref(main_loop);
    
    std::cout << "Программа завершена" << std::endl;
    
    return 0;
}
