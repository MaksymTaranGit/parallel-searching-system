| Запит | Режим | Очікуваний результат | Фактичний результат | Час (мс) | Статус |
| :--- | :---: | :--- | :--- | :---: | :---: |
| `computer` | `keyword` | Топ-10 документів зі словом 'computer'; TF-IDF за спаданням | знайдено 10, Топ-1: 1003.txt (10.8827) | 24 | **PASS** |
| `astronomy telescope` | `keyword` | Документи про астрономію; TF-IDF за спаданням | знайдено 10, Топ-1: 44167.txt ("THE ROYAL OBSERVATORY GREENWICH") (28.3841) | 19 | **PASS** |
| `ocean voyage stars` | `keyword` | Релевантні морські тексти; валідні сніпети | знайдено 10, Топ-1: 6317.txt ("SAILING ALONE AROUND THE WORLD") (17.2823) | 21 | **PASS** |
| `railroad steam` | `keyword` | Документи про залізницю / парові двигуни | знайдено 10, Топ-1: 39329.txt ("SKETCH OF THE BALDWIN LOCOMOTIVE WORKS") (16.3780) | 21 | **PASS** |
| `ocean voyage` | `expanded` | Синоніми від Ollama + Топ-10 документів | синоніми: ['sea trip', 'sea journey', 'sea voyage', 'ocean trip', 'ocean journey']; знайдено 10, Топ-1: 6317.txt (16.1763) | 474 | **PASS** |
| `frustration` | `expanded` | Англійські синоніми без сміття + Топ-10 документів | синоніми: ['frustration', 'stress', 'discomfort', 'annoyance', 'discontent']; знайдено 10, Топ-1: 32394.txt (11.8161) | 487 | **PASS** |
| `space stars` | `ws` | 3 етапи WS: query_expansion -> keyword_search -> done | query_expansion -> keyword_search -> done; синоніми: ['stars', 'galaxies', 'space', 'orbit', 'galactic']; 10 шт., Топ-1: 8172.txt (13.3460) | 17 (пошук) | **PASS** |
| `the and of in` | `keyword` | Порожній список [] (усі слова — стоп-слова) | Повернуто [] | 0 | **PASS** |
| `qwertyzxcv98765` | `keyword` | Порожній список [] (неіснуючий термін) | Повернуто [] | 0 | **PASS** |
