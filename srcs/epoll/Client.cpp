#include "Client.hpp"

Client::Client()
{
    this->fd = -1;
    this->state = IDLE;
    this->bytesRead = 0;
    this->bytesWritten = 0;
    this->previousDataAmount = 0;
    this->erase = false;
}

Client::~Client() {
}

Client::Client(const Client& copy)
{
    *this = copy;
}

Client& Client::operator=(const Client& copy)
{
    if (this != & copy)
    {
        this->fd = copy.fd;
        this->state = copy.state;
        this->timestamp = copy.timestamp;
        this->readBuffer = copy.readBuffer;
        this->rawReadData = copy.rawReadData;
        this->previousDataAmount = copy.previousDataAmount;
        this->writeBuffer = copy.writeBuffer;
        this->bytesRead = copy.bytesRead;
        this->bytesWritten = copy.bytesWritten;
        this->serverInfo = copy.serverInfo;
        this->request = copy.request;
    }
    return *this;
}

void Client::reset()
{
    this->state = IDLE;
    this->readBuffer.clear();
    this->rawReadData.clear();
    this->previousDataAmount = 0;
    this->writeBuffer.clear();
    this->headerString.clear();
    this->bytesRead = 0;
    this->bytesWritten = 0;
    this->erase = false;
    this->request = HTTPRequest();
}

reqTypes Client::getMethodEnum()
{
    if (request.method == "GET")
        return GET;
    if (request.method == "POST")
        return POST;
    if (request.method == "DELETE")
        return DELETE;
    return INVALID;
}
