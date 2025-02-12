#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <tchar.h>

#define MAX_LINE_LENGTH 1024

// Loads the file into memory and returns a pointer to its content along with its size.
char* loadFile(const char* filename, DWORD *pFileSize) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Cannot open file %s\n", filename);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = (char*)malloc(size);
    if (!buffer) {
        fclose(f);
        printf("Memory error\n");
        return NULL;
    }
    size_t read = fread(buffer, 1, size, f);
    fclose(f);
    if (read != size) {
        free(buffer);
        printf("Error reading file\n");
        return NULL;
    }
    *pFileSize = (DWORD)size;
    return buffer;
}

// ---------------------- Functions for KMP search and fragment processing ----------------------

// Computes the longest proper prefix-suffix array for the KMP algorithm.
void computeLPS(const char* pattern, int m, int* lps) {
    int len = 0;
    lps[0] = 0;  // The first element is always 0.
    int i = 1;
    while (i < m) {
        if (pattern[i] == pattern[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0)
                len = lps[len - 1];
            else {
                lps[i] = 0;
                i++;
            }
        }
    }
}

// Uses KMP algorithm to find the pattern in the text.
char* kmpSearch(const char* text, int n, const char* pattern, int m) {
    if (m == 0)
        return (char*)text;
    int* lps = (int*)malloc(m * sizeof(int));
    if (!lps)
        return NULL;
    computeLPS(pattern, m, lps);
    int i = 0, j = 0;
    while (i < n) {
        if (text[i] == pattern[j]) {
            i++; j++;
            if (j == m) {
                free(lps);
                return (char*)(text + i - j);
            }
        } else {
            if (j != 0)
                j = lps[j-1];
            else
                i++;
        }
    }
    free(lps);
    return NULL;
}

// Removes the backslashes from the string.
void removeBackslashes(char* str) {
    int read = 0, write = 0;
    while (str[read]) {
        if (str[read] != '\\')
            str[write++] = str[read];
        read++;
    }
    str[write] = '\0';
}


// ---------------------- Functions to search for an Ethereum address ----------------------

// Checks if a character is a hexadecimal character.
int is_hex_char(char c) {
    return isdigit(c) || (tolower(c) >= 'a' && tolower(c) <= 'f');
}


char* loadFileContent(const char *filePath, long *outSize) {
    FILE *fp = fopen(filePath, "rb");
    if (!fp) {
        perror("Error opening file");
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    rewind(fp);
    char *buffer = (char *)malloc(fileSize + 1);
    if (!buffer) {
        fclose(fp);
        fprintf(stderr, "Memory allocation error\n");
        return NULL;
    }
    size_t read = fread(buffer, 1, fileSize, fp);
    fclose(fp);
    if (read != fileSize) {
        free(buffer);
        fprintf(stderr, "Error reading file\n");
        return NULL;
    }
    buffer[fileSize] = '\0';  // Null-terminate the string.
    if (outSize)
        *outSize = fileSize;
    return buffer;
}




char* findEthereumAddress(char *logFilePath) {
    long fileSize = 0;
    char *fileContent = loadFileContent(logFilePath, &fileSize);
    if (!fileContent)
        return NULL;
    
    // Define the search pattern.
    const char *pattern = "\"address\":\"";
    int patternLen = (int)strlen(pattern);
    
    // Use the KMP search to locate the pattern.
    char *pFound = kmpSearch(fileContent, (int)fileSize, pattern, patternLen);
    if (!pFound) {
        fprintf(stderr, "Pattern not found in file.\n");
        free(fileContent);
        return NULL;
    }
    
    // Move pointer past the found pattern.
    char *addressStart = pFound + patternLen;
    
    // Find the next occurrence of a double-quote which terminates the address.
    char *addressEnd = strchr(addressStart, '\"');
    if (!addressEnd) {
        fprintf(stderr, "Closing delimiter not found.\n");
        free(fileContent);
        return NULL;
    }
    
    // Calculate the length of the address.
    int addressLen = addressEnd - addressStart;
    if (addressLen <= 0 || addressLen > 100) { // A sanity-check; Ethereum addresses are 42 characters.
        fprintf(stderr, "Invalid address length: %d\n", addressLen);
        free(fileContent);
        return NULL;
    }
    
    // Allocate and copy the address.
    char *foundAddress = (char*)malloc(addressLen + 1);
    if (!foundAddress) {
        fprintf(stderr, "Memory allocation error for address\n");
        free(fileContent);
        return NULL;
    }
    strncpy(foundAddress, addressStart, addressLen);
    foundAddress[addressLen] = '\0';
    
    free(fileContent);
    return foundAddress;
}

// Returns the balance (in wei) or -1 in case of error.
long long getEthereumBalance(const char* ethAddress) {
    long long balance = -1;
    char urlPath[256];
    // Form the request path (for example: /v1/eth/main/addrs/<address>/balance)
    snprintf(urlPath, sizeof(urlPath), "/v1/eth/main/addrs/%s/balance", ethAddress);

    // Server name (using the BlockCypher API)
    const char* serverName = "api.blockcypher.com";

    // Initialize WinInet session.
    HINTERNET hInternet = InternetOpenA("WinInet Eth Balance Agent/1.0",
                                          INTERNET_OPEN_TYPE_PRECONFIG, 
                                          NULL, NULL, 0);
    if (!hInternet) {
        fprintf(stderr, "InternetOpenA failed.\n");
        return -1;
    }

    // Connect to the server via HTTPS.
    HINTERNET hConnect = InternetConnectA(hInternet, serverName, INTERNET_DEFAULT_HTTPS_PORT,
                                           NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        fprintf(stderr, "InternetConnectA failed.\n");
        InternetCloseHandle(hInternet);
        return -1;
    }

    // Open the HTTP request (GET method).
    HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET", urlPath,
                                          NULL, NULL, NULL,
                                          INTERNET_FLAG_SECURE, 0);
    if (!hRequest) {
        fprintf(stderr, "HttpOpenRequestA failed.\n");
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return -1;
    }

    // Send the request.
    BOOL bRequestSent = HttpSendRequestA(hRequest,
                                         NULL, 0,       // no additional headers required.
                                         NULL, 0);
    if (!bRequestSent) {
        fprintf(stderr, "HttpSendRequestA failed.\n");
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return -1;
    }

    // Reading the response.
    DWORD dwBytesRead = 0;
    DWORD bufferSize = 4096;
    char buffer[4096];
    char* responseData = NULL;
    size_t totalSize = 0;

    while(TRUE) {
        if (!InternetReadFile(hRequest, buffer, bufferSize, &dwBytesRead)) {
            fprintf(stderr, "InternetReadFile failed.\n");
            break;
        }
        if(dwBytesRead == 0)
            break;

        char* newData = (char*)realloc(responseData, totalSize + dwBytesRead + 1);
        if(!newData) {
            fprintf(stderr, "Memory allocation error for response.\n");
            free(responseData);
            responseData = NULL;
            break;
        }
        responseData = newData;
        memcpy(responseData + totalSize, buffer, dwBytesRead);
        totalSize += dwBytesRead;
        responseData[totalSize] = '\0'; // Null-terminate the string.
    }

    // Free handles.
    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (!responseData) {
        fprintf(stderr, "Failed to receive the response data.\n");
        return -1;
    }

    // Parsing the JSON response: look for the substring "\"balance\":"
    const char* token = "\"balance\":";
    char* tokenPos = strstr(responseData, token);
    if (tokenPos) {
        tokenPos += strlen(token);
        // Expected a number, possibly with spaces – skip any leading spaces.
        while(*tokenPos == ' ' || *tokenPos == '\t') {
            tokenPos++;
        }
        char* endPos = tokenPos;
        // Scan until a delimiter (comma or closing curly brace) is encountered.
        while(*endPos && *endPos != ',' && *endPos != '}')
            endPos++;
        size_t tokenLength = endPos - tokenPos;
        if (tokenLength < 64) {
            char balanceStr[64] = {0};
            strncpy(balanceStr, tokenPos, tokenLength);
            balanceStr[tokenLength] = '\0';
            balance = _strtoui64(balanceStr, NULL, 10);
        }
    } else {
        fprintf(stderr, "Did not find \"balance\" in the response:\n%s\n", responseData);
    }

    free(responseData);
    return balance;
}


// ---------------------- main() ----------------------

int main(int argc, char* argv[]) {

    if (argc < 2) {
        printf("Usage: %s <metamask log filename>\n", argv[0]);
        return 1;
    }
    
    // Open file (searching for a fragment)
    const char* fileName = argv[1];
    FILE* file = fopen(fileName, "rb");
    if (!file) {
        printf("Error opening file: %s\n", fileName);
        return 1;
    }
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char *)malloc(fileSize + 1);
    if (!buffer) {
        printf("Out of memory\n");
        fclose(file);
        return 1;
    }
    
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != fileSize) {
        printf("Error reading file\n");
        free(buffer);
        fclose(file);
        return 1;
    }
    buffer[fileSize] = '\0';
    fclose(file);
    
    int contentSize = (int)fileSize;
    // Pattern to search: "vault":"{\"data\":\""
    const char* fullPattern = "vault\":\"{\\\"data\\\":\\\"";
    int fullPatternLen = (int)strlen(fullPattern);
    char* pos = kmpSearch(buffer, contentSize, fullPattern, fullPatternLen);
    if (!pos) {
        printf("Pattern not found: %s\n", fullPattern);
        free(buffer);
    } else {
        // Skip 8 characters after the found address.
        int skipCount = 8;
        char* trimmedPos = pos + skipCount;
        // Define the end pattern to search for.
        const char* endPattern = "\"},";
        int remainingSize = contentSize - (int)(trimmedPos - buffer);
        char* endPos = kmpSearch(trimmedPos, remainingSize, endPattern, (int)strlen(endPattern));
        if (!endPos) {
            printf("End pattern not found: %s\n", endPattern);
            free(buffer);
        } else {
            endPos += (int)strlen(endPattern);
            int fragmentLength = (int)(endPos - trimmedPos);
            if (fragmentLength > 3)
                fragmentLength -= 3;
            else {
                printf("Fragment is too short to drop the last 3 characters\n");
                free(buffer);
                return 1;
            }
            char* fragment = (char *)malloc(fragmentLength + 1);
            if (!fragment) {
                printf("Out of memory for fragment\n");
                free(buffer);
                return 1;
            }
            memcpy(fragment, trimmedPos, fragmentLength);
            fragment[fragmentLength] = '\0';
            removeBackslashes(fragment);
            printf("\n%s\n", fragment);
            free(fragment);
            free(buffer);
        }
    }
    
    // Search for Ethereum address.
    char* ethAddress = findEthereumAddress((char *)fileName);
    if (ethAddress) {
        printf("\n%s\n", ethAddress);
        // Using the found address (or a test address if needed) for the balance query.
        long long balanceWei = getEthereumBalance(ethAddress);
        if (balanceWei >= 0) {
            // Convert balance from wei to ETH (1 ETH = 1e18 wei).
            double balanceEth = balanceWei / 1e18;
            printf("\nBalance for address %s: %.17f ETH\n", ethAddress, balanceEth);
           
        } else {
            printf("Failed to retrieve balance for address %s\n", ethAddress);
        }
        free(ethAddress);
    } else {
        printf("Ethereum address not found.\n");
    }
    
    return 0;
}
