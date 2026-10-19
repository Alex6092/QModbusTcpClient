# SimpleModbusTcpClient

> English version: [README.md](README.md)

Un petit **client Modbus TCP asynchrone pour Qt 5 et Qt 6**. Il tient en deux fichiers à
ajouter à votre projet. Les résultats arrivent sous forme de signaux Qt, et les erreurs sont
entièrement gérées : exceptions Modbus, timeouts, pertes de connexion, réponses malformées et
paramètres invalides.

- [Fonctionnalités](#fonctionnalités)
- [Ajouter la classe à un projet](#ajouter-la-classe-à-un-projet)
- [Démarrage rapide](#démarrage-rapide)
- [Exemple complet](#exemple-complet)
- [Gestion des erreurs](#gestion-des-erreurs)
- [Configuration](#configuration)
- [Adresses Modbus](#adresses-modbus)
- [Fonctionnement et garanties](#fonctionnement-et-garanties)
- [Lancer les tests](#lancer-les-tests)
- [Migrer depuis QModbusTcpClient](#migrer-depuis-qmodbustcpclient)

## Fonctionnalités

| FC | Méthode | Signal de succès | Limites |
|------|-------------------------------------------|----------------------------------------------------|-------------|
| 0x01 | `readCoils(startAddress, count)` | `coilsRead(transactionId, startAddress, values)` | 1 à 2000 |
| 0x02 | `readDiscreteInputs(startAddress, count)` | `discreteInputsRead(transactionId, startAddress, values)` | 1 à 2000 |
| 0x03 | `readHoldingRegisters(startAddress, count)` | `holdingRegistersRead(transactionId, startAddress, values)` | 1 à 125 |
| 0x04 | `readInputRegisters(startAddress, count)` | `inputRegistersRead(transactionId, startAddress, values)` | 1 à 125 |
| 0x05 | `writeSingleCoil(address, value)` | `singleCoilWritten(transactionId, address, value)` | |
| 0x06 | `writeSingleRegister(address, value)` | `singleRegisterWritten(transactionId, address, value)` | |
| 0x0F | `writeMultipleCoils(startAddress, values)` | `multipleCoilsWritten(transactionId, startAddress, values)` | 1 à 1968 |
| 0x10 | `writeMultipleRegisters(startAddress, values)` | `multipleRegistersWritten(transactionId, startAddress, values)` | 1 à 123 |

- Un seul signal, `requestFailed()`, signale tous les échecs, avec le code d'exception Modbus quand il y en a un.
- Le timeout, l'identifiant d'unité (unit id ou adresse esclave) et le nombre maximal de requêtes simultanées (une file d'attente) sont configurables.
- Les réponses sont analysées de façon robuste : réponses fragmentées, groupées ou dans le désordre, vérification de chaque réponse par rapport à sa requête. Une trame erronée ne bloque jamais le client.
- Qt 5.9 ou plus récent, ou Qt 6. La seule dépendance est le module Qt Network, et un compilateur C++11 suffit.

## Ajouter la classe à un projet

Copiez `simplemodbustcpclient.h` et `simplemodbustcpclient.cpp` dans votre projet, ou ajoutez ce
dépôt comme sous-module git :

```sh
git submodule add https://github.com/Alex6092/QModbusTcpClient.git third_party/SimpleModbusTcpClient
```

### qmake

Incluez le fichier `.pri` fourni dans votre fichier `.pro` :

```qmake
include(third_party/SimpleModbusTcpClient/simplemodbustcpclient.pri)
```

Ou ajoutez les fichiers à la main :

```qmake
QT += network
HEADERS += simplemodbustcpclient.h
SOURCES += simplemodbustcpclient.cpp
```

### CMake

Ajoutez le dossier. Il fournit la cible de bibliothèque statique `SimpleModbusTcpClient` et
utilise la même version de Qt que votre projet :

```cmake
add_subdirectory(third_party/SimpleModbusTcpClient)
target_link_libraries(myapp PRIVATE SimpleModbusTcpClient)
```

Ou ajoutez les fichiers à la main. `AUTOMOC` est obligatoire, car la classe utilise `Q_OBJECT` :

```cmake
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Network)   # ou Qt5
add_executable(myapp main.cpp simplemodbustcpclient.cpp simplemodbustcpclient.h)
target_link_libraries(myapp PRIVATE Qt6::Network)
```

## Démarrage rapide

```cpp
#include "simplemodbustcpclient.h"

auto *modbus = new SimpleModbusTcpClient("192.168.1.10", 502, this);
modbus->setUnitId(1);

connect(modbus, &SimpleModbusTcpClient::connected, this, [modbus]() {
    modbus->readHoldingRegisters(0, 10);   // Registres 40001 à 40010
    modbus->writeSingleCoil(4, true);      // Coil 00005
});

connect(modbus, &SimpleModbusTcpClient::holdingRegistersRead, this,
        [](quint16 /*transactionId*/, quint16 startAddress, const QVector<quint16> &values) {
    qDebug() << "Registres depuis l'adresse" << startAddress << ":" << values;
});

connect(modbus, &SimpleModbusTcpClient::singleCoilWritten, this,
        [](quint16 /*transactionId*/, quint16 address, bool value) {
    qDebug() << "Coil" << address << "passée à" << value;
});

connect(modbus, &SimpleModbusTcpClient::requestFailed, this,
        [](quint16 transactionId, quint8 functionCode, quint16 address,
           SimpleModbusTcpClient::Error error, quint8 exceptionCode) {
    qWarning() << "La requête" << transactionId << "( FC" << functionCode << ", adresse" << address
               << ") a échoué :" << SimpleModbusTcpClient::errorString(error);
    if (error == SimpleModbusTcpClient::ExceptionError)
        qWarning() << "Exception Modbus :" << SimpleModbusTcpClient::exceptionString(exceptionCode);
});

modbus->connectToHost();
```

## Exemple complet

Cette classe interroge dix registres d'entrée toutes les 500 ms, écrit une consigne à la
demande, signale les erreurs et se reconnecte automatiquement quand la connexion est perdue.

```cpp
// devicemonitor.h
#include <QObject>
#include <QTimer>
#include "simplemodbustcpclient.h"

class DeviceMonitor : public QObject
{
    Q_OBJECT

public:
    explicit DeviceMonitor(const QString &host, QObject *parent = nullptr);
    void setSetpoint(quint16 value);

signals:
    void measuresUpdated(const QVector<quint16> &measures);
    void setpointWritten(bool success);

private:
    void poll();
    void onRequestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                         SimpleModbusTcpClient::Error error, quint8 exceptionCode);

    SimpleModbusTcpClient m_modbus;
    QTimer m_pollTimer;
    QTimer m_reconnectTimer;
    quint16 m_setpointRequest = 0;
};
```

```cpp
// devicemonitor.cpp
#include "devicemonitor.h"
#include <QDebug>

DeviceMonitor::DeviceMonitor(const QString &host, QObject *parent)
    : QObject(parent), m_modbus(host, 502)
{
    m_modbus.setUnitId(1);
    m_modbus.setTimeout(500);
    m_modbus.setMaxConcurrentRequests(1);   // Le choix le plus sûr pour un équipement inconnu

    m_pollTimer.setInterval(500);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceMonitor::poll);

    m_reconnectTimer.setInterval(2000);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() { m_modbus.connectToHost(); });

    connect(&m_modbus, &SimpleModbusTcpClient::connected, this, [this]() {
        m_pollTimer.start();
        poll();
    });

    // Connexion perdue ou tentative de connexion échouée : on réessaie plus tard
    connect(&m_modbus, &SimpleModbusTcpClient::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState) {
            m_pollTimer.stop();
            m_reconnectTimer.start();
        }
    });

    connect(&m_modbus, &SimpleModbusTcpClient::socketError, this,
            [](QAbstractSocket::SocketError, const QString &message) {
        qWarning() << "Erreur socket :" << message;
    });

    connect(&m_modbus, &SimpleModbusTcpClient::inputRegistersRead, this,
            [this](quint16, quint16, const QVector<quint16> &values) {
        emit measuresUpdated(values);
    });

    connect(&m_modbus, &SimpleModbusTcpClient::singleRegisterWritten, this,
            [this](quint16 transactionId, quint16, quint16) {
        if (transactionId == m_setpointRequest)
            emit setpointWritten(true);
    });

    connect(&m_modbus, &SimpleModbusTcpClient::requestFailed, this, &DeviceMonitor::onRequestFailed);

    m_modbus.connectToHost();
}

void DeviceMonitor::setSetpoint(quint16 value)
{
    // On garde l'ID de transaction pour reconnaître la réponse
    m_setpointRequest = m_modbus.writeSingleRegister(100, value);   // Registre 40101
}

void DeviceMonitor::poll()
{
    // On n'empile pas les requêtes si l'équipement est plus lent que la période de polling
    if (m_modbus.pendingRequestCount() == 0)
        m_modbus.readInputRegisters(0, 10);   // Registres 30001 à 30010
}

void DeviceMonitor::onRequestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                                    SimpleModbusTcpClient::Error error, quint8 exceptionCode)
{
    QString reason = SimpleModbusTcpClient::errorString(error);
    if (error == SimpleModbusTcpClient::ExceptionError)
        reason += " (" + SimpleModbusTcpClient::exceptionString(exceptionCode) + ")";

    qWarning("Modbus request %u (FC 0x%02X, address %u) failed: %s",
             transactionId, functionCode, address, qPrintable(reason));

    if (transactionId == m_setpointRequest)
        emit setpointWritten(false);
}
```

## Gestion des erreurs

### Le contrat

- Chaque méthode de requête renvoie l'**ID de transaction** de la requête, qui ne vaut jamais 0.
- Plus tard, **exactement un signal** est émis pour cette requête : son signal de succès, ou `requestFailed()`.
- Ces signaux ne sont **jamais émis pendant l'appel de la méthode de requête**. On peut donc toujours mémoriser l'ID renvoyé avant que le résultat arrive.
- Le signal de succès d'une écriture n'est émis qu'une fois l'écriture confirmée par l'équipement : l'écho de la réponse est comparé à la requête.
- Le signal de succès d'une lecture contient toujours exactement le nombre de valeurs demandé.

```cpp
void requestFailed(quint16 transactionId, quint8 functionCode, quint16 address,
                   SimpleModbusTcpClient::Error error, quint8 exceptionCode);
```

`address` est l'adresse (de départ) de la requête, et `exceptionCode` n'est renseigné que si
`error` vaut `ExceptionError`. `SimpleModbusTcpClient::errorString(error)` et
`SimpleModbusTcpClient::exceptionString(exceptionCode)` donnent une description lisible (en anglais).

### Erreurs

| `Error` | Signification |
|------------------------|------------------------------------------------------------------------------|
| `ExceptionError` | L'équipement a répondu par une exception Modbus, voir `exceptionCode` |
| `TimeoutError` | Pas de réponse dans le délai `timeout()`. Une réponse tardive est ignorée |
| `NotConnectedError` | La requête a été faite alors que le client n'était pas connecté (rien n'est envoyé) |
| `ConnectionLostError` | La connexion a été fermée, ou n'a pas pu être établie, avant l'arrivée de la réponse |
| `InvalidRequestError` | Paramètres hors des limites Modbus (rien n'est envoyé) |
| `InvalidResponseError` | Réponse malformée ou qui ne correspond pas à la requête (byte count, écho, code fonction) |
| `WriteError` | La requête n'a pas pu être écrite sur le socket |

### Codes d'exception Modbus

| Code | `ExceptionCode` | Cause habituelle |
|------|---------------------------------------|---------------------------------------------------------------|
| 0x01 | `IllegalFunction` | Le code fonction n'est pas pris en charge par l'équipement |
| 0x02 | `IllegalDataAddress` | L'adresse, ou l'adresse + la quantité, sort de la table de l'équipement |
| 0x03 | `IllegalDataValue` | Une valeur de la requête est refusée (quantité, valeur...) |
| 0x04 | `ServerDeviceFailure` | Erreur irrécupérable dans l'équipement |
| 0x05 | `Acknowledge` | Requête longue acceptée, en cours de traitement |
| 0x06 | `ServerDeviceBusy` | L'équipement est occupé, réessayer plus tard |
| 0x07 | `NegativeAcknowledge` | L'équipement ne peut pas exécuter la fonction de programme demandée |
| 0x08 | `MemoryParityError` | Erreur de cohérence mémoire dans l'équipement |
| 0x0A | `GatewayPathUnavailable` | La passerelle n'a pas de chemin vers la cible (configuration, surcharge) |
| 0x0B | `GatewayTargetDeviceFailedToRespond` | L'équipement derrière la passerelle n'a pas répondu (vérifier l'unit id) |

Les équipements peuvent aussi renvoyer des codes propres au constructeur : c'est pour cela que
`exceptionCode` est un `quint8`.

Les erreurs au niveau du socket (connexion refusée, hôte introuvable...) sont relayées par le
signal `socketError(QAbstractSocket::SocketError, QString)`.

## Configuration

| Réglage | Défaut | Description |
|----------------------------------|--------|-------------|
| `setUnitId(quint8)` | `0` | Identifiant d'unité (adresse esclave) placé dans les requêtes faites après l'appel. Avec une passerelle Modbus TCP vers RTU, mettre l'adresse de l'esclave. Pour joindre plusieurs esclaves derrière une même passerelle, l'appeler avant chaque requête |
| `setTimeout(int msec)` | `1000` | Délai maximal d'attente d'une réponse, compté à partir de l'envoi de la requête. `0` ou moins désactive le timeout |
| `setMaxConcurrentRequests(int)` | `0` | Nombre maximal de requêtes envoyées sans avoir reçu leur réponse. Les suivantes attendent dans une file. `0` signifie sans limite |
| `setHost()` / `setPort()` | | Utilisés par le prochain `connectToHost()` |

> **Quelle valeur pour `setMaxConcurrentRequests()` ?** Modbus TCP autorise plusieurs requêtes
> en cours en même temps, mais beaucoup d'équipements et de passerelles ne répondent qu'à la
> première de plusieurs requêtes reçues ensemble et ignorent les autres sans rien dire (c'est le
> cas du serveur `pymodbus`, par exemple). Les requêtes ignorées se terminent alors par
> `TimeoutError`. Si vous constatez ce comportement, ou si vous ne connaissez pas l'équipement,
> utilisez `setMaxConcurrentRequests(1)`.

Autres fonctions utiles :

- `connectToHost()`, `connectToHost(host, port)`, `disconnectFromHost()`, `state()` et `isConnected()`.
- Les signaux `connected()`, `disconnected()`, `stateChanged()` et `socketError()`.
- `pendingRequestCount()` : le nombre de requêtes dont le signal n'a pas encore été émis. Pratique pour éviter d'empiler les requêtes.
- `socket()` : le `QTcpSocket` interne, **réservé aux réglages avancés**. Ne jamais lire ni écrire directement dessus.

  ```cpp
  modbus->socket()->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
  ```

- Les messages sont journalisés dans la catégorie `simplemodbus.tcpclient`. Par exemple, `QT_LOGGING_RULES="simplemodbus.tcpclient.warning=false"` masque les avertissements.

## Adresses Modbus

Les méthodes utilisent les **adresses du protocole, qui commencent à 0**. Les documentations des
équipements utilisent souvent la notation « Modicon », où le premier chiffre indique la table et
où la numérotation commence à 1 :

| Table | Notation | Méthodes | Exemple |
|-------------------|----------------|-----------------------------------------------------|----------------------------------|
| Coils (bobines) | 00001 à 09999 | `readCoils`, `writeSingleCoil`, `writeMultipleCoils` | La coil 00005 est l'adresse 4 |
| Entrées TOR (discrete inputs) | 10001 à 19999 | `readDiscreteInputs` | L'entrée 10001 est l'adresse 0 |
| Registres d'entrée (input registers) | 30001 à 39999 | `readInputRegisters` | Le registre 30010 est l'adresse 9 |
| Registres de maintien (holding registers) | 40001 à 49999 | `readHoldingRegisters`, `writeSingleRegister`, `writeMultipleRegisters` | Le registre 40108 est l'adresse 107 |

Les registres font 16 bits. Une valeur sur 32 bits occupe deux registres, et l'ordre des mots
dépend de l'équipement :

```cpp
const quint32 raw = (quint32(values[0]) << 16) | values[1];   // Mot de poids fort en premier
float measure;
memcpy(&measure, &raw, sizeof(measure));                       // #include <cstring>
```

## Fonctionnement et garanties

- Tout est asynchrone et rien ne bloque. Utilisez le client depuis le thread auquel il appartient, qui doit faire tourner une boucle d'événements.
- Les requêtes faites pendant la connexion sont mises en file et envoyées dès que la connexion est établie.
- Les requêtes sont envoyées dans l'ordre des appels. Les réponses peuvent arriver dans n'importe quel ordre : elles sont associées à leur requête grâce à l'ID de transaction.
- À la fermeture de la connexion, toutes les requêtes en attente (envoyées ou en file) se terminent par `ConnectionLostError`, et le buffer de réception est vidé avant la connexion suivante.
- Un en-tête de trame invalide (protocol id différent de 0, ou longueur impossible) vide le buffer de réception au lieu de bloquer le client.
- La destruction du client abandonne ses requêtes en attente sans émettre de signal.

## Lancer les tests

Le dossier `tests/` contient des tests unitaires (Qt Test) qui tournent contre un faux serveur Modbus :

```sh
cmake -S . -B build
cmake --build build
cd build && ctest --output-on-failure
```

Qt 6 est utilisé s'il est disponible. Ajoutez `-DSIMPLEMODBUSTCPCLIENT_QT_VERSION=5` à la
première commande pour forcer Qt 5.

## Migrer depuis QModbusTcpClient

Cette classe remplace l'ancienne `QModbusTcpClient`. Le nouveau nom évite le conflit avec la
classe `QModbusTcpClient` du module Qt Serial Bus. L'API a été repensée :

| Avant | Maintenant |
|-------|------------|
| `#include "qmodbustcpclient.h"`, `QModbusTcpClient` | `#include "simplemodbustcpclient.h"`, `SimpleModbusTcpClient` |
| Hérite de `QTcpSocket` | Hérite de `QObject`. Utiliser `connected()`, `disconnected()`, `stateChanged()`, `socketError()`, `state()`, `isConnected()` et `socket()` |
| Unit id toujours à 0 | `setUnitId()` |
| (absent) | `readCoils()` / `coilsRead()` (FC 0x01) |
| `readMultipleInputsStatusFC2()` / `onReadMultipleInputsStatusSentence(start, values)` | `readDiscreteInputs()` / `discreteInputsRead(transactionId, start, values)` |
| `readMultipleHoldingRegistersFC3()` / `onReadMultipleHoldingRegistersSentence(start, values)` | `readHoldingRegisters()` / `holdingRegistersRead(transactionId, start, values)` |
| `readMultipleInputRegistersFC4()` / `onReadMultipleInputRegistersSentence(start, values)` | `readInputRegisters()` / `inputRegistersRead(transactionId, start, values)` |
| `on...SentenceSingleValue(address, value)` | Supprimés : boucler sur `values` (l'adresse de `values[i]` est `start + i`) |
| `forceSingleCoilFC5()` / `onForceSingleCoilSentence(success, address, value)` | `writeSingleCoil()` / `singleCoilWritten(transactionId, address, value)` |
| `writeSingleWordFC6()` / `onWriteSingleWordSentence(success, address, value)` | `writeSingleRegister()` / `singleRegisterWritten(transactionId, address, value)` |
| `forceMultipleCoilsFC15()` / `onForceMultipleCoilsSentence(success, start, values, count)` | `writeMultipleCoils()` / `multipleCoilsWritten(transactionId, start, values)` |
| `presetMultipleRegistersFC16()` / `onPresetMultipleRegistersSentence(success, start, values, count)` | `writeMultipleRegisters()` / `multipleRegistersWritten(transactionId, start, values)` |
| `success == false` | `requestFailed()`, qui signale aussi les exceptions, les timeouts et les déconnexions |

La refonte corrige aussi plusieurs bugs de l'ancienne version :

- Une réponse dont l'octet de longueur valait 0x80 ou plus faisait boucler la réception à l'infini. C'était le cas, par exemple, d'une lecture de 63 registres ou plus.
- Le FC 0x10 n'envoyait qu'une partie de la trame, avec un byte count faux. Il ne fonctionnait jamais et désynchronisait la connexion.
- Le FC 0x0F avec une liste vide débordait d'un buffer.
- Les réponses d'exception et les requêtes sans réponse étaient ignorées sans rien dire, et ces dernières fuyaient en mémoire.
- Après une reconnexion, une réponse incomplète restée dans le buffer corrompait les réponses suivantes.
