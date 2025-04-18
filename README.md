# Захват видео, обработка и трансляция с использованием GStreamer и OpenCV

## Описание
Приложение для захвата видео с камеры, его обработки с помощью алгоритмов OpenCV и трансляции обработанных кадров по UDP.

## Возможности
- Захват видео с камеры через GStreamer
- Конвертация между форматами GStreamer и OpenCV
- Обработка видео с использованием алгоритмов OpenCV
- Кодирование в H.264 и трансляция по UDP

## Требования
- GStreamer 1.0 с плагинами
- OpenCV 4.x
- Компилятор C++ с поддержкой C++11
- Камера с поддержкой V4L2 (Linux) или DirectShow (Windows)

## Компиляция
```bash
g++ -o video_processor main.cpp `pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 opencv4`
```

## Управление
- ESC - выход из программы

## Архитектура
- Конвейер источника: v4l2src → videoconvert → videoscale → appsink
- Обработка кадров с помощью OpenCV
- Конвейер назначения: appsrc → videoconvert → x264enc → rtph264pay → udpsink

## Авторы
- Марк <li4nomark228@gmail.com> - разработка конвертора GStreamer/OpenCV и реализация передачи через UDP
- Иван <ivanmalyj1994@gmail.com> - интеграция захвата с камеры и обработка с помощью OpenCV
