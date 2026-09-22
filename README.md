# GDZLauncher
Простой лаунчер для майна.

---
## Роадмап

- [ ] Кроссплатформенность
	- [ ] Linux
		- [x] WebKitGtk
		- [ ] Работа с майном
		- [ ] Система обновления
	- [ ] Windows
		- [x] Webview2
		- [ ] Работа с майном
		- [ ] Система обновления
- [ ] Сборка из под линуха
	- [ ] Упаковка
		- [ ] Linux
		- [ ] Windows
	- [x] Компиляция кода
		- [x] Linux
		- [x] Windows
- [ ] Запуск майна
	- [ ] Пользовательские настройки
- [ ] Конфигурация при сборке
	- [ ] Установка адреса сервера API с каналом новостей и обновления
	- [ ] Лок сервера на который ходит клиент игры
- [ ] Дока по API для лаунчера

---
## Сборка
**Зависимости:**
> - cmake
> - webkitgtk-6.0
> - mingw-w64-gcc
> - Прямые руки

### Linux
```bash
mkdir build && cd build
cmake ..
make
```

### Windows
```bash
mkdir build_win && cd build_win
cmake -DCMAKE_TOOLCHAIN_FILE=../mingw-w64.cmake -S . -B build
make
```

