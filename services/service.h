// definitia serviciului SOAP si a tipurilor de date folosite
// acest fisier este folosit de soapcpp2 pentru a genera codul necesar

// tipul de date pentru date binare encodate in base64
struct xsd__base64Binary {
  unsigned char *__ptr; // pointer catre datele binare
  int __size;           // dimensiunea datelor in bytes
  char *id;             // identificator optional
  char *type;           // tipul MIME optional
  char *options;        // optiuni aditionale
};

// un fisier cu numele si continutul sau
struct ns__FileItem {
  char *filename;                // numele fisierului
  struct xsd__base64Binary data; // continutul fisierului encodat base64
};

// un array de fisiere trimise spre analiza
struct ns__ArrayOfFiles {
  struct ns__FileItem *__ptr; // pointer catre array-ul de fisiere
  int __size;                 // numarul de fisiere
};

// gsoap ns service name: service
// gsoap ns service protocol: SOAP
// gsoap ns service style: rpc
// gsoap ns service encoding: encoded
// gsoap ns service namespace: http://example.com/service.wsdl
// gsoap ns schema namespace: urn:service

// gsoap ns service method: Analizeaza fisierele si returneaza raportul de
// plagiat
int ns__analyzeFiles(struct ns__ArrayOfFiles files, char **report);
