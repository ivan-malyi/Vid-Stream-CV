#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <iostream>
#include <string>

// Предполагаем, что следующие функции и структуры определены в основном коде
extern cv::Mat gst_sample_to_mat(GstSample* sample);
extern GstSample* mat_to_gst_sample(const cv::Mat &frame, GstCaps *caps);
extern cv::Mat process_frame(const cv::Mat &input_frame);

// Тестовые функции
bool test_gstreamer_initialization() {
    std::cout << "Тест инициализации GStreamer... ";
    
    int argc = 0;
    char **argv = NULL;
    gst_init(&argc, &argv);
    
    // Попробуем создать простой элемент
    GstElement *element = gst_element_factory_make("fakesrc", "test_source");
    if (!element) {
        std::cout << "ОШИБКА: Не удалось создать элемент GStreamer\n";
        return false;
    }
    
    gst_object_unref(element);
    std::cout << "ПРОЙДЕН\n";
    return true;
}

bool test_camera_connection() {
    std::cout << "Тест подключения камеры... ";
    
    // Создаем простой конвейер для проверки доступа к камере
    GstElement *pipeline = gst_parse_launch("v4l2src num-buffers=1 ! fakesink", NULL);
    if (!pipeline) {
        std::cout << "ОШИБКА: Не удалось создать конвейер\n";
        return false;
    }
    
    // Пробуем запустить и проверить состояние
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cout << "ОШИБКА: Не удалось установить состояние READY\n";
        gst_object_unref(pipeline);
        return false;
    }
    
    // Возвращаем в состояние NULL и освобождаем ресурсы
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    
    std::cout << "ПРОЙДЕН\n";
    return true;
}

bool test_opencv_processing() {
    std::cout << "Тест обработки OpenCV... ";
    
    // Создаем тестовое изображение
    cv::Mat test_image(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    
    // Рисуем фигуры для теста обнаружения контуров
    cv::rectangle(test_image, cv::Rect(100, 100, 200, 200), cv::Scalar(255, 255, 255), -1);
    cv::circle(test_image, cv::Point(450, 240), 80, cv::Scalar(200, 0, 0), -1);
    
    // Тестируем функцию обработки
    cv::Mat processed = process_frame(test_image);
    
    if (processed.empty()) {
        std::cout << "ОШИБКА: Функция обработки вернула пустое изображение\n";
        return false;
    }
    
    // Отображаем результат, если указан флаг визуализации
    if (true) {
        cv::imshow("Тестовое изображение", test_image);
        cv::imshow("Обработанное изображение", processed);
        cv::waitKey(2000); // Показываем на 2 секунды
        cv::destroyAllWindows();
    }
    
    std::cout << "ПРОЙДЕН\n";
    return true;
}

bool test_gst_opencv_conversion() {
    std::cout << "Тест конвертации GStreamer <-> OpenCV... ";
    
    // Создаем тестовое изображение
    cv::Mat original(480, 640, CV_8UC3, cv::Scalar(50, 100, 150));
    cv::putText(original, "Test Image", cv::Point(50, 50), 
                cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
    
    // Создаем caps для конвертации
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                      "format", G_TYPE_STRING, "RGB",
                                      "width", G_TYPE_INT, 640,
                                      "height", G_TYPE_INT, 480,
                                      NULL);
    
    // Конвертируем Mat -> GstSample
    GstSample *sample = mat_to_gst_sample(original, caps);
    if (!sample) {
        std::cout << "ОШИБКА: Не удалось конвертировать Mat в GstSample\n";
        gst_caps_unref(caps);
        return false;
    }
    
    // Конвертируем GstSample -> Mat
    cv::Mat converted = gst_sample_to_mat(sample);
    gst_sample_unref(sample);
    gst_caps_unref(caps);
    
    if (converted.empty()) {
        std::cout << "ОШИБКА: Не удалось конвертировать GstSample в Mat\n";
        return false;
    }
    
    // Проверяем размеры
    if (original.rows != converted.rows || original.cols != converted.cols) {
        std::cout << "ОШИБКА: Размеры изображения изменились после конвертации\n";
        return false;
    }
    
    // Отображаем результат для визуального сравнения
    if (true) {
        cv::imshow("Оригинал", original);
        cv::imshow("После конвертации", converted);
        cv::waitKey(2000);
        cv::destroyAllWindows();
    }
    
    std::cout << "ПРОЙДЕН\n";
    return true;
}

bool test_udp_streaming() {
    std::cout << "Тест UDP потока... ";
    
    // Создаем конвейер для отправки тестового потока
    GstElement *pipeline = gst_parse_launch(
        "videotestsrc num-buffers=30 ! video/x-raw,width=320,height=240 ! "
        "x264enc ! rtph264pay ! udpsink host=127.0.0.1 port=5000", NULL);
    
    if (!pipeline) {
        std::cout << "ОШИБКА: Не удалось создать конвейер UDP\n";
        return false;
    }
    
    // Запускаем конвейер
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cout << "ОШИБКА: Не удалось запустить UDP поток\n";
        gst_object_unref(pipeline);
        return false;
    }
    
    // Ждем 2 секунды для отправки пакетов
    std::cout << "Отправка тестового UDP потока...";
    g_usleep(2000000);
    
    // Останавливаем и освобождаем
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    
    std::cout << "ПРОЙДЕН\n";
    return true;
}

int main(int argc, char** argv) {
    std::cout << "=== Запуск тестов для системы обработки видео ===\n";
    
    int passed = 0;
    int total = 5;
    
    if (test_gstreamer_initialization()) passed++;
    if (test_camera_connection()) passed++;
    if (test_opencv_processing()) passed++;
    if (test_gst_opencv_conversion()) passed++;
    if (test_udp_streaming()) passed++;
    
    std::cout << "\n=== Результаты тестов ===\n";
    std::cout << "Пройдено: " << passed << "/" << total << " тестов\n";
    
    if (passed == total) {
        std::cout << "УСПЕХ: Все тесты пройдены!\n";
        return 0;
    } else {
        std::cout << "ВНИМАНИЕ: Некоторые тесты не пройдены.\n";
        return 1;
    }
}
