#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "dirindex.h"
#include "parsereq.h"
#include "servefile.h"

#define BUFMAX 4096

// Gerar HTML com a listagem de arquivos e enviar pelo socket
int listDirectory(int sockfd, char *host, char *baseDir, char *path){
	DIR *dir;
	struct dirent *entry;

	char buffer[BUFMAX];
	snprintf(buffer, BUFMAX, "%s/%s", baseDir, path);

	dir = opendir(buffer);
	if(dir == NULL){
		perror("opendir failed");
		return 1;
	}

	char *responseBody = malloc(1);
	if(!responseBody){
		perror("malloc failed");
		closedir(dir);
		return 1;
	}

	ssize_t partLen = snprintf(buffer, BUFMAX,
		"<!DOCTYPE html><html>\r\n"
		"<style>table {margin-left: auto; margin-right: auto; width: 50%%;}</style>\r\n"
		"<body>\r\n"
		"<h1>Index of <i>/%s</i></h1>\r\n"
		"<table>",
		path
	);

	char *tmp = realloc(responseBody, 1+partLen);
	if(!tmp){
		perror("realloc failed");
		free(responseBody);
		closedir(dir);
		return 1;
	} responseBody = tmp;

	memcpy(responseBody, buffer, partLen);
	size_t responseBodyLen = partLen;

	// Processar cada arquivo na pasta
	while((entry = readdir(dir))){
		// Pular entradas "." e ".."
		if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")){
			continue;
		}
		char filename[BUFMAX], url[BUFMAX];
		snprintf(filename, BUFMAX, "%s/%s%s", baseDir, path, entry->d_name);
		snprintf(url, BUFMAX, "http://%s/%s%s", host, path, entry->d_name);

		struct stat statbuf;
		if(stat(filename, &statbuf) == -1){
			perror(filename);
			free(responseBody);
			closedir(dir);
			return 1;
		}

		partLen = snprintf(buffer, BUFMAX,
			"<tr>\r\n"
			"<td><a href=\"%s\">%s</a></td>"
			"<td>%s</td>"
			"<td>%.2f KiB</td>\r\n"
			"<tr>\r\n",
			url, entry->d_name,
			ctime(&statbuf.st_mtime),
			statbuf.st_size/1024.f
		);

		char *tmp = realloc(responseBody, 1+responseBodyLen+partLen);
		if(!tmp){
			perror("realloc failed");
			free(responseBody);
			closedir(dir);
			return 1;
		} responseBody = tmp;

		memcpy(responseBody + responseBodyLen, buffer, partLen);
		responseBodyLen += partLen;
	}

	const char responseEnd[] = "</table></body></html>\r\n";

	tmp = realloc(responseBody, 1+responseBodyLen+sizeof(responseEnd));
	if(!tmp){
			perror("realloc failed");
			free(responseBody);
			closedir(dir);
			return 1;
	} responseBody = tmp;

	memcpy(responseBody+responseBodyLen, responseEnd, sizeof(responseEnd));
	responseBodyLen += sizeof(responseEnd);

	// Gerar cabeçalho HTTP
	char header[BUFMAX];
	ssize_t headerLen = snprintf(header, BUFMAX,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: %ld\r\n"
		"\r\n",
		responseBodyLen
	);

	ssize_t hwritten = write(sockfd, header,(size_t)headerLen);
	if(hwritten != headerLen){
		// Envio parcial do cabeçalho
		fprintf(stderr, "Error: partial write of HTTP header.\n");
		free(responseBody);
		closedir(dir);
		close(sockfd);
		return 1;
	}

	// Loop para resolver envio parcial do buffer
	size_t totalWritten = 0;
	while(totalWritten < responseBodyLen){
		ssize_t written = write(sockfd, responseBody+totalWritten, responseBodyLen-totalWritten);
		if(written == -1){
			perror("write");
			close(sockfd);
			closedir(dir);
			return 1;
		}
		totalWritten += (size_t)written;
	}

	free(responseBody);
	close(sockfd);
	closedir(dir);
	return 0;
}

//Se o endereço da página não termina com "/", redirecionar
int redirectToDir(int sockfd, char *host, char *dirname){
	char response[BUFMAX];
	ssize_t responseLen = snprintf(response, BUFMAX,
		"HTTP/1.1 301 Moved Permanently\r\n"
		"Location: http://%s/%s/\r\n"
		"Content-Length: 0\r\n"
		"\r\n",
		host, dirname
	);

	ssize_t totalWritten = 0;
	while(totalWritten < responseLen){
		ssize_t written = write(sockfd, response+totalWritten, responseLen-totalWritten);
		if(written == -1){
			perror("write");
			close(sockfd);
			return 1;
		}
		totalWritten += written;
	}

	close(sockfd);
	return 0;
}

// Servir página "index.html" ou gerá-la dinamicamente
int serveDirIndex(int sockfd, char *baseDir, ParsedRequest *preq, uint8_t listDir){
	if(!preq || !baseDir){
		fprintf(stderr, "Error: invalid arguments.\n");
		return 1;
	}

	if(preq->path[strlen(preq->path)-1] != '/'){
		return redirectToDir(sockfd, preq->host, preq->path);
	}

	char dirname[BUFMAX];
	snprintf(dirname, BUFMAX, "%s/%s", baseDir, preq->path);

	char indexname[BUFMAX];
	snprintf(indexname, BUFMAX, "%s/index.html", dirname);

	struct stat statbuf;
	if(stat(indexname, &statbuf) != -1){
		return serveFile(sockfd, indexname);
	} else{
		if(!listDir) return 2; // 403 Forbidden
		else return listDirectory(sockfd, preq->host, baseDir, preq->path);
	}
}