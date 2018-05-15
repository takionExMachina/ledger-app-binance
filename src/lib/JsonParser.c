/*******************************************************************************
*   (c) 2016 Ledger
*   (c) 2018 ZondaX GmbH
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include <jsmn.h>
#include "JsonParser.h"

bool Match(
        const char* jsonString,
        jsmntok_t token,
        const char* reference,
        int size)
{
    bool sizeOkay =(token.end - token.start) == size;
    const char* str = jsonString + token.start;
    return (sizeOkay && strncmp(str, reference, size) == 0);
}

// Known issues:
// 1. The same block of code is duplicated for inputs and outputs - this could be further refactored
// 2. Parsing logic supports a varying number of inputs, outputs and coins. Input and output arrays can be
//    found at random positions in the stream - offsets are not hardcoded. Internal layouts of individual
//    inputs and outputs are however hardcoded and must not change. 

bool IsChild(int potentialChildIndex, int parentIndex, jsmntok_t* tokens)
{
    return (tokens[potentialChildIndex].start > tokens[parentIndex].start)
        && (tokens[potentialChildIndex].end < tokens[parentIndex].end);
}

void ParseMessage(
        parsed_json_t* parsedMessage,
        const char* jsonString)
{
    const char inputsTokenName[] = "inputs";
    int inputsTokenSize = sizeof(inputsTokenName)-1; // minus null terminating char

    const char outputsTokenName[] = "outputs";
    int outputsTokenSize = sizeof(outputsTokenName)-1; // minus null terminating char

    bool processedInputs = false;
    bool processedOutputs = false;

    int i = 0;
    while (i < parsedMessage->NumberOfTokens) {
        if (parsedMessage->Tokens[i].type == JSMN_UNDEFINED){
            break;
        }
        // Parse inputs array
        if (!processedInputs && Match(jsonString, parsedMessage->Tokens[i], inputsTokenName, inputsTokenSize)) {
            int inputsArrayIndex = i + 1;
            parsedMessage->NumberOfInputs = 0;

            // Parse input
            int inputIndex = inputsArrayIndex + 1;
            while (IsChild(inputIndex, inputsArrayIndex, parsedMessage->Tokens)) {

                // Parse single input
                parsedMessage->Inputs[parsedMessage->NumberOfInputs].Address = inputIndex + 2;

                // Parse Coins array
                int coinsArrayIndex = inputIndex + 4;
                parsedMessage->Inputs[parsedMessage->NumberOfInputs].NumberOfCoins = 0;

                // Parse Coin
                int coinIndex = coinsArrayIndex + 1;
                while (IsChild(coinIndex, coinsArrayIndex, parsedMessage->Tokens)) {

                    parsedMessage->Inputs[parsedMessage->NumberOfInputs].Coins[parsedMessage->Inputs[parsedMessage->NumberOfInputs].NumberOfCoins].Denum =
                            coinIndex + 2;

                    parsedMessage->Inputs[parsedMessage->NumberOfInputs].Coins[parsedMessage->Inputs[parsedMessage->NumberOfInputs].NumberOfCoins++].Amount =
                            coinIndex + 4;

                    coinIndex += 5;
                }

                //parsedMessage->Inputs[parsedMessage->NumberOfInputs].Sequence = coinIndex + 1;
                parsedMessage->NumberOfInputs++;

                inputIndex = coinIndex;
            }
            i = inputIndex;
            processedInputs = true;
        }

        // Parse outputs array
        if (!processedOutputs && Match(jsonString, parsedMessage->Tokens[i], outputsTokenName, outputsTokenSize)) {
            int outputsArrayIndex = i + 1;
            parsedMessage->NumberOfOutputs = 0;

            // Parse output
            int outputIndex = outputsArrayIndex + 1;
            while (IsChild(outputIndex, outputsArrayIndex, parsedMessage->Tokens)) {

                // Parse single input
                parsedMessage->Outputs[parsedMessage->NumberOfOutputs].Address = outputIndex + 2;

                // Parse Coins array
                int coinsArrayIndex = outputIndex + 4;
                parsedMessage->Outputs[parsedMessage->NumberOfOutputs].NumberOfCoins = 0;

                // Parse Coin
                int coinIndex = coinsArrayIndex + 1;
                while (IsChild(coinIndex, coinsArrayIndex, parsedMessage->Tokens)) {

                    parsedMessage->Outputs[parsedMessage->NumberOfOutputs].Coins[parsedMessage->Outputs[parsedMessage->NumberOfOutputs].NumberOfCoins].Denum =
                            coinIndex + 2;

                    parsedMessage->Outputs[parsedMessage->NumberOfOutputs].Coins[parsedMessage->Outputs[parsedMessage->NumberOfOutputs].NumberOfCoins++].Amount =
                            coinIndex + 4;

                    coinIndex += 5;
                }

                parsedMessage->NumberOfOutputs++;
                outputIndex = coinIndex;
            }
            i = outputIndex - 1;
            processedOutputs = true;
        }

        i++;
    }
}

void ParseJson(parsed_json_t *parsedJson, const char *jsonString) {
    jsmn_parser parser;
    jsmn_init(&parser);

    parsedJson->NumberOfTokens = jsmn_parse(
            &parser,
            jsonString,
            strlen(jsonString),
            parsedJson->Tokens,
            MAX_NUMBER_OF_TOKENS);

    parsedJson->CorrectFormat = false;
    if (parsedJson->NumberOfTokens >= 1
        &&
        parsedJson->Tokens[0].type != JSMN_OBJECT) {
        parsedJson->CorrectFormat = true;
    }

    // Build SendMsg representation with links to tokens
    ParseMessage(parsedJson, jsonString);
}

void ParseSignedMsg(parsed_json_t* parsedMessage, const char* signedMsg)
{
    jsmn_parser parser;
    jsmn_init(&parser);

    parsedMessage->NumberOfTokens = jsmn_parse(
            &parser,
            signedMsg,
            strlen(signedMsg),
            parsedMessage->Tokens,
            MAX_NUMBER_OF_TOKENS);

    parsedMessage->CorrectFormat = false;
    if (parsedMessage->NumberOfTokens >= 1
        &&
        parsedMessage->Tokens[0].type != JSMN_OBJECT) {
        parsedMessage->CorrectFormat = true;
    }

}

int TransactionMsgGetInfo(
        char *name,
        char *value,
        int index,
        const parsed_json_t* parsed_transaction,
        unsigned int* view_scrolling_total_size,
        unsigned int view_scrolling_step,
        unsigned int max_chars_per_line,
        const char* message,
        void(*copy)(void* dst, const void* source, unsigned int size))
{
    int currentIndex = 0;
    for (int i = 0; i < parsed_transaction->NumberOfInputs; i++) {
        if (index == currentIndex) {
            copy((char *) name, "Input address", sizeof("Input address"));

            unsigned int addressSize =
                    parsed_transaction->Tokens[parsed_transaction->Inputs[i].Address].end -
                    parsed_transaction->Tokens[parsed_transaction->Inputs[i].Address].start;

            *view_scrolling_total_size = addressSize;
            const char *addressPtr =
                    message +
                    parsed_transaction->Tokens[parsed_transaction->Inputs[i].Address].start;

            if (view_scrolling_step < addressSize) {
                copy(
                        (char *) value,
                        addressPtr + view_scrolling_step,
                        addressSize < max_chars_per_line ? addressSize : max_chars_per_line);
                value[addressSize] = '\0';
            }
            return currentIndex;
        }
        currentIndex++;
        for (int j = 0; j < parsed_transaction->Inputs[i].NumberOfCoins; j++) {
            if (index == currentIndex) {
                copy((char *) name, "Coin", sizeof("Coin"));

                int coinSize =
                        parsed_transaction->Tokens[parsed_transaction->Inputs[i].Coins[j].Denum].end -
                        parsed_transaction->Tokens[parsed_transaction->Inputs[i].Coins[j].Denum].start;

                const char *coinPtr =
                        message +
                        parsed_transaction->Tokens[parsed_transaction->Inputs[i].Coins[j].Denum].start;

                copy((char *) value, coinPtr, coinSize);
                value[coinSize] = '\0';
                return currentIndex;
            }
            currentIndex++;
            if (index == currentIndex) {
                copy((char *) name, "Amount", sizeof("Amount"));

                int coinAmountSize =
                        parsed_transaction->Tokens[parsed_transaction->Inputs[i].Coins[j].Amount].end -
                        parsed_transaction->Tokens[parsed_transaction->Inputs[i].Coins[j].Amount].start;

                const char *coinAmountPtr =
                        message +
                        parsed_transaction->Tokens[parsed_transaction->Inputs[i].Coins[j].Amount].start;

                copy((char *) value, coinAmountPtr, coinAmountSize);
                value[coinAmountSize] = '\0';
                return currentIndex;
            }
            currentIndex++;
        }
    }
    for (int i = 0; i < parsed_transaction->NumberOfOutputs; i++) {
        if (index == currentIndex) {
            copy((char *) name, "Output address", sizeof("Output address"));

            unsigned int addressSize =
                    parsed_transaction->Tokens[parsed_transaction->Outputs[i].Address].end -
                    parsed_transaction->Tokens[parsed_transaction->Outputs[i].Address].start;

            const char *addressPtr =
                    message +
                    parsed_transaction->Tokens[parsed_transaction->Outputs[i].Address].start;

            *view_scrolling_total_size = addressSize;

            if (view_scrolling_step < addressSize) {
                copy(
                        (char *) value,
                        addressPtr + view_scrolling_step,
                        addressSize < max_chars_per_line ? addressSize : max_chars_per_line);
                value[addressSize] = '\0';
            }
            return currentIndex;
        }
        currentIndex++;
        for (int j = 0; j < parsed_transaction->Outputs[i].NumberOfCoins; j++) {
            if (index == currentIndex) {
                copy((char *) name, "Coin", sizeof("Coin"));

                int coinSize =
                        parsed_transaction->Tokens[parsed_transaction->Outputs[i].Coins[j].Denum].end -
                        parsed_transaction->Tokens[parsed_transaction->Outputs[i].Coins[j].Denum].start;

                const char *coinPtr =
                        message +
                        parsed_transaction->Tokens[parsed_transaction->Outputs[i].Coins[j].Denum].start;

                copy((char *) value, coinPtr, coinSize);
                value[coinSize] = '\0';
                return currentIndex;
            }
            currentIndex++;
            if (index == currentIndex) {
                copy((char *) name, "Amount", sizeof("Amount"));

                int coinAmountSize =
                        parsed_transaction->Tokens[parsed_transaction->Outputs[i].Coins[j].Amount].end -
                        parsed_transaction->Tokens[parsed_transaction->Outputs[i].Coins[j].Amount].start;

                const char *coinAmountPtr =
                        message +
                        parsed_transaction->Tokens[parsed_transaction->Outputs[i].Coins[j].Amount].start;

                copy((char *) value, coinAmountPtr, coinAmountSize);
                value[coinAmountSize] = '\0';
                return currentIndex;
            }
            currentIndex++;
        }
    }
    return currentIndex;
}

int SignedMsgGetInfo(
        char *name,
        char *value,
        int index,
        const parsed_json_t* parsed_message,
        unsigned int* view_scrolling_total_size,
        unsigned int view_scrolling_step,
        unsigned int max_chars_per_line,
        const char* message,
        void(*copy)(void* dst, const void* source, unsigned int size))
{
    if (index * 2 + 1 < parsed_message->NumberOfTokens) {
        jsmntok_t start_token = parsed_message->Tokens[1 + index * 2];
        jsmntok_t end_token = parsed_message->Tokens[1 + index * 2 + 1];
        copy((char *) name,
             message + start_token.start,
             start_token.end - start_token.start);
        copy((char *) value,
             message + end_token.start,
             end_token.end - end_token.start);
    }
    else {
        strcpy(name, "Error");
        strcpy(value, "Out-of-bounds");
    }
}