#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parsereq.h"
#include "urlencode.h"

#define BUFMAX 4096

// Busca case-insensitive de um campo no cabeçalho HTTP
char *findHeaderField(char *header, char *headerEnd, char *fieldName){
	while(header < headerEnd){
		char *lineEnd = memchr(header, '\n', headerEnd - header);
		if(!lineEnd) break;

		int len = strlen(fieldName);
		if(
			lineEnd - header >= len &&
			strncasecmp(header, fieldName, len) == 0
		){
			char *val = header + len;
			while(val < lineEnd && (*val == ' ' || *val == '\t')) val++;
			return val;
		}
		header = lineEnd + 1;
	}
	return NULL;
}

ParsedRequest *createParsedRequest(){
	ParsedRequest *preq = malloc(sizeof(ParsedRequest));
	if(!preq){
		perror("malloc failed");
		return NULL;
	};
	preq->host = NULL;
	preq->path = NULL;

	return preq;
}

void freeParsedRequest(ParsedRequest *preq){
	if(!preq) return;
	free(preq->host);
	free(preq->path);
	free(preq);
}

/* Ler campos URI e Host da requisição.
	Retorna 0 para sucesso,
	1 para 400 Bad Request,
	2 para 501 Not Implemented,
	3 para 500 Internal Server Error */
int parseRequest(char *req, ParsedRequest *preq){
	if(!preq){
		fprintf(stderr, "Error: argument preq is NULL.\n");
		return 3;
	}

	// Verificar se a primeira linha da requisição termina com "HTTP/1.1"
	char *tmp;
	if(!(tmp = strstr(req, "\r\n"))){
		fprintf(stderr, "Error: malformed request header.\n");
		freeParsedRequest(preq);
		return 1;
	} else{
		char *httpVerPtr = strstr(req, " HTTP/1.1\r\n");
		if(!httpVerPtr || tmp <= httpVerPtr){
			fprintf(stderr, "Error: malformed request header.\n");
			freeParsedRequest(preq);
			return 1;
		}
	}

	// Checar método
	if(strncmp(req, "GET ", 4)){
		const char *const methods[] = {"OPTIONS ", "HEAD ", "POST ", "PUT ", "DELETE ", "TRACE ", "CONNECT "};
		freeParsedRequest(preq);
		for(size_t i=0;i<(sizeof(methods)/sizeof(void*));i++){
			if(!strncmp(req, methods[i], strlen(methods[i])))
				return 2; // 501 Not Implemented
		}
		return 1; // 400 Bad Request
	} else{
		// Extrair URI
		char *uriStartPtr = 5+req; // Pular "GET /"
		char *uriEndPtr = strchr(uriStartPtr, ' ');
		if(!uriEndPtr){
			fprintf(stderr, "Error: malformed request header.\n");
			freeParsedRequest(preq);
			return 1;
		}

		char decodedPath[BUFMAX];
		*uriEndPtr = '\x00';

		if(urlDecode(uriStartPtr, decodedPath, BUFMAX)){
			fprintf(stderr, "Error: failed to decode URL.\n");
			freeParsedRequest(preq);
			return 1;
		}

		*uriEndPtr = ' ';

		preq->path = malloc(1 + strlen(decodedPath));
		if(!preq->path){
			perror("malloc failed");
			freeParsedRequest(preq);
			return 3;
		}
		strncpy(preq->path, decodedPath, strlen(decodedPath));

		preq->path[uriEndPtr-uriStartPtr] = '\x00';
	}

	// Extrair host
	char *headerEnd = 4+strstr(req,"\r\n\r\n");
	if(!headerEnd){
		fprintf(stderr, "Error: malformed request header.\n");
		freeParsedRequest(preq);
		return 1;
	}
	char *host = findHeaderField(req, headerEnd, "Host:");
	if(!host){
		fprintf(stderr, "Error: request header is missing 'Host' field.\n");
		freeParsedRequest(preq);
		return 1;
	} else{
		char *hostEnd = strstr(host, "\r\n");
		if(!hostEnd){
			fprintf(stderr, "Error: malformed request header.\n");
			freeParsedRequest(preq);
			return 1;
		}
		preq->host = malloc(hostEnd-host);
		if(!preq->host){
			perror("malloc failed");
			freeParsedRequest(preq);
			return 3;
		}
		strncpy(preq->host, host, hostEnd-host);
		preq->host[hostEnd-host] = '\x00';
	}
	return 0;
}