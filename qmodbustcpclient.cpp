#include "qmodbustcpclient.h"
#include <QDebug>
#include <QtMath>

#define LENGTH_IDX 4
#define UNIT_IDENTIFIER_IDX 6
#define FUNCTION_IDX 7

ModbusRequest::ModbusRequest(QModbusTcpClient * client, quint16 transactionId)
{
    this->client = client;
    this->transactionId = transactionId;
}

quint16 ModbusRequest::getTransactionId()
{
    return this->transactionId;
}

ModbusRequest::~ModbusRequest() {}

// FC6 :

WriteSingleWordFC6Request::WriteSingleWordFC6Request(QModbusTcpClient * client, quint16 transactionId, quint16 wordAddress, quint16 wordValue)
    : ModbusRequest (client, transactionId)
{
    this->wordAddress = wordAddress;
    this->wordValue = wordValue;
}

WriteSingleWordFC6Request::~WriteSingleWordFC6Request() {}

void WriteSingleWordFC6Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint16 address = (extractedData[8] << 8) | extractedData[9];
        quint16 value = (extractedData[10] << 8) | extractedData[11];

        bool success = (wordAddress == address && wordValue == value);
        getClient()->onWriteSingleWordSentence(success, address, value);
    }
    else {
        qDebug() << "WriteSingleWordFC6Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// FC 3
ReadMultipleHoldingRegistersFC3Request::ReadMultipleHoldingRegistersFC3Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, quint16 nbWord)
    : ModbusRequest (client, transactionId)
{
    this->startAddress = startAddress;
    this->nbWord = nbWord;
}

ReadMultipleHoldingRegistersFC3Request::~ReadMultipleHoldingRegistersFC3Request() {}

void ReadMultipleHoldingRegistersFC3Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint8 numberOfDataBytes = extractedData[8];
        quint8 numberOfWord = numberOfDataBytes / 2;

        QVector<quint16> values;

        for(int i = 0; i < numberOfWord; i++)
        {
            int idx = 9 + (i * 2);
            quint16 currentValue = (extractedData[idx] << 8) | extractedData[idx + 1];
            values.push_back(currentValue);
            getClient()->onReadMultipleHoldingRegistersSentenceSingleValue(startAddress + i, currentValue);
        }

        getClient()->onReadMultipleHoldingRegistersSentence(this->startAddress, values);
    }
    else {
        qDebug() << "ReadMultipleHoldingRegistersFC3Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// FC 4
ReadMultipleInputRegistersFC4Request::ReadMultipleInputRegistersFC4Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, quint16 nbWord)
    : ModbusRequest (client, transactionId)
{
    this->startAddress = startAddress;
    this->nbWord = nbWord;
}

ReadMultipleInputRegistersFC4Request::~ReadMultipleInputRegistersFC4Request() {}

void ReadMultipleInputRegistersFC4Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint8 numberOfDataBytes = extractedData[8];
        quint8 numberOfWord = numberOfDataBytes / 2;

        QVector<quint16> values;

        for(int i = 0; i < numberOfWord; i++)
        {
            int idx = 9 + (i * 2);
            quint16 currentValue = (extractedData[idx] << 8) | extractedData[idx + 1];
            values.push_back(currentValue);
            getClient()->onReadMultipleInputRegistersSentenceSingleValue(startAddress + i, currentValue);
        }

        getClient()->onReadMultipleInputRegistersSentence(this->startAddress, values);
    }
    else {
        qDebug() << "ReadMultipleInputRegistersFC4Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// FC 5 :
ForceSingleCoilsFC5Request::ForceSingleCoilsFC5Request(QModbusTcpClient * client, quint16 transactionId, quint16 coilAddress, bool value)
    : ModbusRequest (client, transactionId)
{
    this->coilAddress = coilAddress;
    this->value = value;
}

ForceSingleCoilsFC5Request::~ForceSingleCoilsFC5Request() {}

void ForceSingleCoilsFC5Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint16 address = (extractedData[8] << 8) | extractedData[9];
        quint16 value = (extractedData[10] << 8) | extractedData[11];


        bool success = this->coilAddress == address && ((this->value && value == 0xFF00) || (!this->value && value == 0x0000));
        getClient()->onForceSingleCoilSentence(success, address, value == 0xFF00);
    }
    else {
        qDebug() << "ForceSingleCoilsFC5Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// FC 15 :
ForceMultipleCoilsFC15Request::ForceMultipleCoilsFC15Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, QVector<bool> values)
    : ModbusRequest (client, transactionId)
{
    this->startAddress = startAddress;
    this->values = values;
}

ForceMultipleCoilsFC15Request::~ForceMultipleCoilsFC15Request() {}

void ForceMultipleCoilsFC15Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint16 address = (extractedData[8] << 8) | extractedData[9];
        quint16 numberOfCoilsWritten = (extractedData[10] << 8) | extractedData[11];

        bool success = this->startAddress == address && this->values.size() == numberOfCoilsWritten;
        getClient()->onForceMultipleCoilsSentence(success, startAddress, values, numberOfCoilsWritten);
    }
    else {
        qDebug() << "ForceMultipleCoilsFC15Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// FC 02 :
ReadMultipleInputsStatusFC2Request::ReadMultipleInputsStatusFC2Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, quint16 nbValues)
    : ModbusRequest (client, transactionId)
{
    this->startAddress = startAddress;
    this->nbInputs = nbValues;
}

ReadMultipleInputsStatusFC2Request::~ReadMultipleInputsStatusFC2Request() {}

void ReadMultipleInputsStatusFC2Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint8 nbByteToRead = extractedData[8];
        QVector<bool> values;

        int nbBitRead = 0;
        for(int i = 0; i < nbByteToRead; i++)
        {
            for(int j = 0; j < 8; j++)
            {
                if(nbBitRead < nbInputs)
                {
                    values.push_back(QModbusTcpClient::getBit(extractedData[9 + i], j));
                    nbBitRead++;
                }
                else
                {
                    break;
                }
            }
        }

        getClient()->onReadMultipleInputsStatusSentence(startAddress, values);
    }
    else {
        qDebug() << "ReadMultipleInputsStatusFC2Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// FC 16 :
PresetMultipleRegisterFC16Request::PresetMultipleRegisterFC16Request(QModbusTcpClient * client, quint16 transactionId, quint16 startAddress, QVector<quint16> values)
    : ModbusRequest (client, transactionId)
{
    this->startAddress = startAddress;
    this->values = values;
}

PresetMultipleRegisterFC16Request::~PresetMultipleRegisterFC16Request() {}

void PresetMultipleRegisterFC16Request::decodeAndCallback(QVector<unsigned char> extractedData)
{
    quint16 protocolId = (extractedData[2] << 8) | extractedData[3];
    quint8 unitIdentifier = extractedData[UNIT_IDENTIFIER_IDX];
    quint8 functionCode = extractedData[FUNCTION_IDX];

    if(functionCode == getFunctionCode())
    {
        quint16 address = (extractedData[8] << 8) | extractedData[9];
        quint16 numberOfRegistersWritten = (extractedData[10] << 8) | extractedData[11];

        bool success = this->startAddress == address && this->values.size() == numberOfRegistersWritten;
        getClient()->onPresetMultipleRegistersSentence(success, startAddress, values, numberOfRegistersWritten);
    }
    else {
        qDebug() << "PresetMultipleRegisterFC16Request::decodeAndCallback - Received an incoherent sentence to request.";
    }
}

// ModbusClient :
QModbusTcpClient::QModbusTcpClient(QString host, quint16 port, QObject *parent) : QTcpSocket(parent)
{
    this->transactionId = 1;
    this->host = host;
    this->port = port;
    this->unitId = 0;
    QObject::connect(this, SIGNAL(readyRead()), this, SLOT(onDataRecv()));
}

void QModbusTcpClient::connectToHost()
{
    QTcpSocket::connectToHost(host, port);
}

void QModbusTcpClient::onDataRecv()
{
    bool hasReceivedData = false;

    while(bytesAvailable() > 0)
    {
        QByteArray receivedData = read(64);
        for(int i = 0; i < receivedData.size(); i++)
        {
            buffer.push_back(receivedData[i]);
            hasReceivedData = true;
        }
    }

    if(hasReceivedData)
    {
        processModbusSentence();
    }
}

void QModbusTcpClient::processModbusSentence()
{
    bool hasDataToProcess = true;

    while(hasDataToProcess)
    {
        if(buffer.size() >= 6)
        {
            int dataSize = (buffer[LENGTH_IDX] << 8) | buffer[LENGTH_IDX + 1];
            int totalLength = 6 + dataSize;

            if(buffer.size() < totalLength)
            {
                hasDataToProcess = false;
            }
            else {
                QVector<unsigned char> extractedData;
                for(int i = 0; i < totalLength; i++)
                {
                    extractedData.push_back(buffer.front());
                    buffer.pop_front();
                }

                quint16 transactionId = (extractedData[0] << 8) | extractedData[1];

                if(pendingRequests.contains(transactionId))
                {
                    ModbusRequest * request = pendingRequests[transactionId];
                    pendingRequests.remove(transactionId);
                    request->decodeAndCallback(extractedData);
                    delete request;
                }
                else {
                    qDebug() << "Received sentence to unknown request ...";
                }
            }
        }
        else {
            hasDataToProcess = false;
        }
    }
}

bool QModbusTcpClient::getBit(char byte, int bitNumber)
{
    quint8 value = (byte >> bitNumber) & 0x01;
    return value != 0;
}

void QModbusTcpClient::setBit(char * byte, int bitNumber, bool value)
{
    quint8 mask = (1 << bitNumber);
    quint8 orValue = 0 + (value ? mask : 0);
    *byte |= orValue;
}

char QModbusTcpClient::getMSB(quint16 value)
{
    return (value & 0xFF00) >> 8;
}

char QModbusTcpClient::getLSB(quint16 value)
{
    return (value & 0x00FF);
}

quint16 QModbusTcpClient::setTransactionId(char *ptr)
{
    quint16 id = transactionId;
    ptr[0] = getMSB(transactionId);
    ptr[1] = getLSB(transactionId);
    transactionId++;
    return id;
}

void QModbusTcpClient::setProtocolId(char *ptr)
{
    ptr[0] = 0x00;
    ptr[1] = 0x00;
}

void QModbusTcpClient::setLength(char *ptr, quint16 len)
{
    ptr[0] = getMSB(len);
    ptr[1] = getLSB(len);
}

void QModbusTcpClient::setUnitId(char *ptr, quint8 unitId)
{
    ptr[0] = unitId;
}

void QModbusTcpClient::setFunctionCode(char *ptr, quint8 functionCode)
{
    ptr[0] = functionCode;
}

quint16 QModbusTcpClient::setHeader(char *ptr, quint16 length, quint8 unitId, quint8 functionCode)
{
    quint16 id = setTransactionId(&ptr[0]);
    setProtocolId(&ptr[2]);
    setLength(&ptr[4], length);
    setUnitId(&ptr[6], unitId);
    setFunctionCode(&ptr[7], functionCode);
    return id;
}

void QModbusTcpClient::writeSingleWordFC6(quint16 wordAddress, quint16 wordValue)
{
    char trame[12];
    quint16 id = setHeader(trame, 6, unitId, 0x06);
    trame[8] = getMSB(wordAddress);
    trame[9] = getLSB(wordAddress);
    trame[10] = getMSB(wordValue);
    trame[11] = getLSB(wordValue);

    pendingRequests[id] = new WriteSingleWordFC6Request(this, id, wordAddress, wordValue);
    QByteArray data(trame, 12);
    write(data);
    flush();
}

void QModbusTcpClient::readMultipleHoldingRegistersFC3(quint16 startAddress, quint16 nbWord)
{
    char trame[12];
    quint16 id = setHeader(trame, 6, unitId, 0x03);
    trame[8] = getMSB(startAddress);
    trame[9] = getLSB(startAddress);
    trame[10] = getMSB(nbWord);
    trame[11] = getLSB(nbWord);

    pendingRequests[id] = new ReadMultipleHoldingRegistersFC3Request(this, id, startAddress, nbWord);
    QByteArray data(trame, 12);
    write(data);
    flush();
}

void QModbusTcpClient::readMultipleInputRegistersFC4(quint16 startAddress, quint16 nbWord)
{
    char trame[12];
    quint16 id = setHeader(trame, 6, unitId, 0x04);
    trame[8] = getMSB(startAddress);
    trame[9] = getLSB(startAddress);
    trame[10] = getMSB(nbWord);
    trame[11] = getLSB(nbWord);

    pendingRequests[id] = new ReadMultipleInputRegistersFC4Request(this, id, startAddress, nbWord);
    QByteArray data(trame, 12);
    write(data);
    flush();
}

void QModbusTcpClient::forceSingleCoilFC5(quint16 coilAddress, bool value)
{
    char trame[12];
    quint16 id = setHeader(trame, 6, unitId, 0x05);
    trame[8] = getMSB(coilAddress);
    trame[9] = getLSB(coilAddress);
    trame[10] = value ? 0xFF : 0x00;
    trame[11] = 0x00;

    pendingRequests[id] = new ForceSingleCoilsFC5Request(this, id, coilAddress, value);
    QByteArray data(trame, 12);
    write(data);
    flush();
}

void QModbusTcpClient::forceMultipleCoilsFC15(quint16 startAddress, QVector<bool> values)
{
    quint32 nbByteToCode = qCeil((qreal)values.size() / (qreal)8.0);
    if(nbByteToCode > 255)
    {
        qDebug() << "QModbusTcpClient::forceMultipleCoilsFC15 - There is too much values to write ... Operation aborted.";
    }
    else {
        quint16 nbValues = values.size();
        quint16 length = 13 + nbByteToCode;
        char * trame = new char[length];
        quint16 id = setHeader(trame, length - 6, unitId, 0x0F);
        trame[8] = getMSB(startAddress);
        trame[9] = getLSB(startAddress);
        trame[10] = getMSB(nbValues);
        trame[11] = getLSB(nbValues);
        trame[12] = nbByteToCode;

        int byteIdxToCode = 13;
        trame[byteIdxToCode] = 0;
        for(int i = 0; i < nbValues; i++)
        {
            if(i != 0 && i % 8 == 0)
            {
                byteIdxToCode++;
                trame[byteIdxToCode] = 0;
            }

            setBit(&trame[byteIdxToCode], i % 8, values[i]);
        }

        pendingRequests[id] = new ForceMultipleCoilsFC15Request(this, id, startAddress, values);
        QByteArray data(trame, length);
        write(data);
        flush();
        delete trame;
    }
}

void QModbusTcpClient::readMultipleInputsStatusFC2(quint16 startAddress, quint16 nbInput)
{
    char trame[12];
    quint16 id = setHeader(trame, 6, unitId, 0x02);
    trame[8] = getMSB(startAddress);
    trame[9] = getLSB(startAddress);
    trame[10] = getMSB(nbInput);
    trame[11] = getLSB(nbInput);

    pendingRequests[id] = new ReadMultipleInputsStatusFC2Request(this, id, startAddress, nbInput);
    QByteArray data(trame, 12);
    write(data);
    flush();
}

void QModbusTcpClient::presetMultipleRegistersFC16(quint16 startAddress, QVector<quint16> values)
{
    if(values.size() > 255)
    {
        qDebug() << "QModbusTcpClient::presetMultipleRegistersFC16 - There is too much values to write ... Operation aborted.";
    }
    else
    {
        quint16 length = 13 + 2 * values.size();
        char * trame = new char[13 + 2 * values.size()];
        quint16 id = setHeader(trame, length - 6, unitId, 0x10);
        trame[8] = getMSB(startAddress);
        trame[9] = getLSB(startAddress);
        trame[10] = getMSB(values.size());
        trame[11] = getLSB(values.size());
        trame[12] = values.size();

        for(int i = 0; i < values.size(); i++)
        {
            int currentIdx = 13 + (2 * i);
            trame[currentIdx] = getMSB(values[i]);
            trame[currentIdx + 1] = getLSB(values[i]);
        }

        pendingRequests[id] = new PresetMultipleRegisterFC16Request(this, id, startAddress, values);
        QByteArray data(trame, 12);
        write(data);
        flush();
        delete trame;
    }
}
