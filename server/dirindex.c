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
#include "urlencode.h"

#define BUFMAX 4096

// Enviar um chunk usando chunked encoding
int sendChunk(int sockfd, const char *data, size_t len){
	char header[32];
	int headerLen = snprintf(header, sizeof(header), "%zx\r\n", len);

	if(write(sockfd, header, headerLen) != headerLen) return 1;
	if(len > 0 && write(sockfd, data, len) != (ssize_t)len) return 1;
	if(write(sockfd, "\r\n", 2) != 2) return 1;

	return 0;
}

// Gerar HTML com a listagem de arquivos e enviar pelo socket
int listDirectory(int sockfd, char *host, char *baseDir, char *path){
	DIR *dir;
	struct dirent *entry;
	char buffer[BUFMAX];

	snprintf(buffer, BUFMAX, "%s/%s", baseDir, path);
	dir = opendir(buffer);
	if(!dir){
		perror("opendir failed");
		return 1;
	}

	// Cabeçalho HTTP
	ssize_t headerLen = snprintf(buffer, BUFMAX,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n"
	);

	if(write(sockfd, buffer, headerLen) != headerLen){
		closedir(dir);
		close(sockfd);
		return 1;
	}

	// Chunk do cabeçalho HTML
	ssize_t len = snprintf(buffer, BUFMAX,
		"<!DOCTYPE html><html>\r\n"
		"<style>table {margin-left: auto; margin-right: auto; width: 50%%;}</style>\r\n"
		"<body>\r\n"
		"<h1>Index of <i>/%s</i></h1>\r\n"
		"<table>\r\n",
		path
	);

	if(sendChunk(sockfd, buffer, len)){
		closedir(dir);
		close(sockfd);
		return 1;
	}

	// Entradas de diretório
	while((entry = readdir(dir))){
		if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;

		char filename[BUFMAX];
		char uri[BUFMAX];
		char encodedUri[BUFMAX];

		snprintf(filename, BUFMAX, "%s/%s%s", baseDir, path, entry->d_name);
		snprintf(uri, BUFMAX, "%s%s", path, entry->d_name);

		if(urlEncode(uri, encodedUri, BUFMAX)){
			fprintf(stderr, "Error: Failed to encode URI.\n");
			closedir(dir);
			close(sockfd);
			return 1;
		}

		struct stat statbuf;
		if(stat(filename, &statbuf) == -1){
			perror(filename);
			closedir(dir);
			close(sockfd);
			return 1;
		}

		len = snprintf(buffer, BUFMAX,
			"<tr>\r\n"
			"<td><a href=\"http://%s/%s\">%s</a></td>"
			"<td>%s</td>"
			"<td>%.2f KiB</td>\r\n"
			"</tr>\r\n",
			host,
			encodedUri,
			entry->d_name,
			ctime(&statbuf.st_mtime),
			statbuf.st_size / 1024.f
		);

		if(sendChunk(sockfd, buffer, len)){
			closedir(dir);
			close(sockfd);
			return 1;
		}
	}

	// Rodapé HTML
	const char *footer = "</table></body></html>\r\n";
	if(sendChunk(sockfd, footer, strlen(footer))){
		closedir(dir);
		close(sockfd);
		return 1;
	}

	// Chunk final
	write(sockfd, "0\r\n\r\n", 5);

	closedir(dir);
	close(sockfd);
	return 0;
}

//Se o endereço da página não termina com "/", redirecionar
int redirectToDir(int sockfd, char *host, char *dirname){
	char response[BUFMAX], encodedUri[BUFMAX];

	if(urlEncode(dirname, encodedUri, BUFMAX)){
		close(sockfd);
		return 1;
	}

	ssize_t responseLen = snprintf(response, BUFMAX,
		"HTTP/1.1 301 Moved Permanently\r\n"
		"Location: http://%s/%s/\r\n"
		"Content-Length: 0\r\n"
		"\r\n",
		host, encodedUri
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