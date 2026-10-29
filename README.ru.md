# vardiff

[English](README.md) | **Русский**

Header-only библиотека на C++20 для отслеживания выбранных полей объекта,
сравнения независимых объектов и восстановления сохранённых состояний.
Внешних зависимостей нет.

## Сборка и проверка

Нужны компилятор C++20 и CMake 3.20 или новее.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/vardiff_player
./build/vardiff_events
./build/vardiff_nested
```

## Пример

```cpp
#include <vardiff/vardiff.hpp>

#include <iostream>

struct Player {
    int hp = 100;
    int coins = 0;
};

int main() {
    Player player;

    auto watcher = vardiff::track(
        player,
        vardiff::field("hp", &Player::hp),
        vardiff::field("coins", &Player::coins)
    );

    player.hp = 75;

    if (watcher.changed()) {
        for (const auto& change : watcher.diff()) {
            std::cout << change.name << ": "
                      << change.before << " -> "
                      << change.after << '\n';
        }
    }

    watcher.snapshot();
}
```

Результат: hp: 100 -> 75. Исходный файл: [examples/player.cpp](examples/player.cpp).

- track() сохраняет начальные значения выбранных полей.
- changed() проверяет наличие отличий через `operator==`.
- diff() возвращает изменённые поля с принадлежащими результату значениями
  name, before и after; сохранённый снимок не меняется.
- snapshot() сохраняет текущие значения и добавляет снимок в историю.
- Поддерживаются int, float, double, bool, std::string и собственные копируемые
  типы со сравнением, зарегистрированные структуры и контейнеры.

## Callbacks и poll()

Для нового объекта `Player player;`:

```cpp
auto watcher = vardiff::track(
    player, vardiff::watch_options{.auto_snapshot = true, .history_limit = 32},
    vardiff::field("hp", &Player::hp),
    vardiff::field("coins", &Player::coins));

watcher.ignore("coins");
watcher.on_change([](const auto& changes) {
    for (const auto& change : changes) {
        std::cout << change.name << ": " << change.before
                  << " -> " << change.after << '\n';
    }
});
watcher.on_change("hp", [](const vardiff::change& change) {
    std::cout << "new hp: " << change.after.get<int>() << '\n';
});

player.hp = 75; // Здесь callbacks не вызываются.
watcher.poll(); // Проверка, callbacks, затем сохранение проверенного состояния.
```

Callbacks вызываются только в poll(). changed(), diff(), snapshot(), undo()
и redo() callbacks не вызывают. При отсутствии отличий poll() возвращает пустой
список, не вызывает callbacks и не добавляет снимок.

Автообновление по умолчанию включено. Его можно отключить через
watch_options{.auto_snapshot = false} или watcher.set_auto_snapshot(false).
Тогда повторный poll() повторно сообщает об отличиях от прежнего снимка,
пока не будет вызван snapshot().

Полный пример с пользовательским компаратором: [examples/events.cpp](examples/events.cpp).

## История и восстановление

```cpp
watcher.undo();                    // Предыдущий сохранённый снимок.
watcher.redo();                    // Следующий снимок.
watcher.rollback();                // То же, что undo().
watcher.rollback(0);               // Конкретный снимок в текущей истории.
watcher.set_history_limit(16);     // Лимит включает текущий снимок.

const auto states = watcher.history(); // Независимые копии tuple значений полей.
const auto count = watcher.history_size(); // Без копирования.
const auto position = watcher.history_position();
```

Начальный снимок входит в историю. По умолчанию сохраняются до 64 снимков.
После undo() новый snapshot() или успешный poll() с автообновлением удаляет
ветку redo. Несохранённые изменения при восстановлении отбрасываются.
Восстановление требует возможности присваивать выбранным полям сохранённые значения.

## Вложенные структуры и контейнеры

Регистрируйте поля специализацией `vardiff::schema<T>`. `operator==` для такой
структуры не обязателен:

```cpp
struct Item {
    std::string name;
    double price;
};

template <>
struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(
            vardiff::field("name", &Item::name),
            vardiff::field("price", &Item::price));
    }
};
```

Поле типа Item, `std::vector<Item>`, `std::optional<Item>` или `std::array<Item, N>`
будет сравниваться рекурсивно. Контейнеры можно вкладывать друг в друга.
Поддерживаются также деревья значений вида `Node { std::vector<Node> children; }`.
Изменения получают пути items[2].price, selected.name или levels[0].

- Элементы vector и array сравниваются по индексу.
- Вставка в конец vector создаёт `change_kind::added`, удаление — removed.
  Изменение существующего значения имеет вид modified.
- Отсутствующее значение обозначается пустым `vardiff::value`:
  !change.before.has_value() для добавления, !change.after.has_value()
  для удаления. При выводе оно отображается как `<missing>`.
- Если optional присутствует в обоих состояниях, сравнивается его содержимое;
  при смене наличия выдаётся одно изменение по пути самого optional.
- ignore("items[1].price") исключает отдельное поле, ignore("items") — всю ветвь.

Для зарегистрированного корневого типа доступен track(object) без списка полей.
Полный пример: [examples/nested.cpp](examples/nested.cpp).

## Независимые объекты

```cpp
auto changes = vardiff::diff(before, after,
    vardiff::field("hp", &Player::hp),
    vardiff::field("coins", &Player::coins));
bool different = vardiff::changed(before, after, vardiff::field("hp", &Player::hp));
```

Для зарегистрированных типов достаточно vardiff::diff(before, after)
и vardiff::changed(before, after). Оба объекта остаются неизменными.

## Пользовательское сравнение

Третий аргумент field() — предикат равенства: true означает равные значения.

```cpp
vardiff::field("speed", &Player::speed,
    [](double a, double b) { return std::abs(a - b) < 0.01; })
```

Этот фрагмент использует Player из [examples/events.cpp](examples/events.cpp)
и требует `<cmath>`. Компаратор заменяет сравнение всего поля; такое поле выдаёт
одно изменение без рекурсивного обхода. Допускаются собственные типы без
`operator==`, если передан компаратор.

before и after сохраняют исходные типы: `change.before.get<int>()`,
`change.after.get<std::string>()` или `get<MyType>()`. `operator<<` учитывает настройки
потока. Собственные типы могут определять `operator<<`; незарегистрированный тип
без него выводится как `<value>`.

## Подключение

Скопируйте include/vardiff в свой каталог заголовков или используйте CMake:

```cmake
add_subdirectory(path/to/vardiff)
target_link_libraries(my_app PRIVATE vardiff::vardiff)
```

При add_subdirectory примеры и тесты по умолчанию отключены.
Параметры сборки: VARDIFF_BUILD_TESTS, VARDIFF_BUILD_EXAMPLES,
VARDIFF_BUILD_BENCHMARKS. Benchmark по умолчанию выключен.

Заголовок можно использовать напрямую:

```sh
mkdir -p build-direct
g++ -std=c++20 -Iinclude examples/player.cpp -o build-direct/player
./build-direct/player
```

## Дополнительные сведения

- [Контракты API, порядок callbacks, история и исключения](docs/API.ru.md)
- [Производительность и benchmark](docs/performance.ru.md)

Наблюдатель заимствует объект: тот должен оставаться живым по тому же адресу.
Снимки и результаты diff владеют копиями значений. Используется обычная семантика
копирования соответствующего типа. Проверки выполняются при вызовах методов.
Автоматической рефлексии, фонового наблюдения, многопоточности и JSON нет.

## Установка CMake-пакета

```sh
cmake --install build --prefix ./install
```

В проекте-потребителе:

```cmake
find_package(vardiff 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE vardiff::vardiff)
```

При настройке потребителя передайте путь установки через
`-DCMAKE_PREFIX_PATH=/путь/к/install`. Пакет допускает перенос каталога установки.

## Лицензия

Изменения версий: [CHANGELOG.md](CHANGELOG.md).

Copyright 2026 Dmitry. Лицензия [Apache-2.0](LICENSE); сведения об авторстве
находятся в [NOTICE](NOTICE).
