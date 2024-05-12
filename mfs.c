#include <netinet/in.h>
#include "udp.h"
#include <sys/time.h>
#include <unistd.h>
#include "server.h"
#include "mfs.h"
#include "stdlib.h"
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>

char* HostNamee;
int PortNumber;
int IsNameLengthValid(char* name);
int sendPayload(Payload *sentPayload, Payload *responseOfP, int MaxTri) {
    int sockd = UDP_Open(0);
    if (sockd < -1) {
        return -1;
    }

    struct sockaddr_in addr;
    int rc = UDP_FillSockAddr(&addr, HostNamee, PortNumber);
    if (rc < 0) {
        return -1;
    }

    fd_set read_fds;
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    for (int attempt = 0; attempt < MaxTri; ++attempt) {
        FD_ZERO(&read_fds);
        FD_SET(sockd, &read_fds);

        // Envoi de la charge utile
        UDP_Write(sockd, &addr, (char*)sentPayload, sizeof(Payload));

        // Attente de la réponse
        rc = select(sockd + 1, &read_fds, NULL, NULL, &timeout);
        if (rc > 0) {
            // Réception de la réponse
            rc = UDP_Read(sockd, NULL, (char*)responseOfP, sizeof(Payload));
            if (rc > 0) {
                UDP_Close(sockd);
                return 0;
            }
        } else {
            // Gestion du délai d'attente dépassé
            MaxTri -= 1;
        }
    }

    // Fermeture du socket en cas d'échec après MaxTri tentatives
    UDP_Close(sockd);
    return -1;
}


int MFS_Init(char *hostname, int port) {
    // Libérer la mémoire si HostNamee pointe déjà quelque part
    if (HostNamee != NULL) {
        free(HostNamee);
        HostNamee = NULL;
    }

    // Vérifier si le nom d'hôte est valide
    if (hostname == NULL || strlen(hostname) == 0) {
        return -1;  // Nom d'hôte invalide
    }

    // Allouer de la mémoire pour le nom d'hôte
    HostNamee = malloc(strlen(hostname) + 1);
    if (HostNamee == NULL) {
        return -1;  // Échec de l'allocation mémoire
    }

    // Copier le nom d'hôte dans la mémoire allouée
    strcpy(HostNamee, hostname);
    
    // Mettre à jour le numéro de port
    PortNumber = port;

    return 0;  // Initialisation réussie
}


int MFS_Lookup(int pinum, char *name) {
    // Vérifier si le nom n'est pas accepté
    if (IsNameLengthValid(name) < 0) {
        return -1;
    }

    Payload sentPayload;
    Payload responseOfP;

    // Initialiser les champs du payload
    sentPayload.inodeNum = pinum;
    sentPayload.Opayload = 0;
    strncpy(sentPayload.name, name, sizeof(sentPayload.name) - 1);
    sentPayload.name[sizeof(sentPayload.name) - 1] = '\0';  // S'assurer de la null-termination

    // Appeler la fonction sendPayload
    int rc = sendPayload(&sentPayload, &responseOfP, 3);
    if (rc < 0) {
        return -1;
    }

    return responseOfP.inodeNum;
}


int MFS_Stat(int inum, MFS_Stat_t *m) {
    // Vérifier la validité de la structure MFS_Stat_t fournie en argument
    if (m == NULL) {
        return -1;  // Retourner une erreur si la structure est invalide
    }

    // Créer le payload et la réponse
    Payload sentPayload;
    Payload responseOfP;

    // Remplir les champs du payload
    sentPayload.inodeNum = inum;
    sentPayload.Opayload = 1;

    // Appeler la fonction sendPayload
    if (sendPayload(&sentPayload, &responseOfP, 3) < 0) {
        return -1;  // En cas d'échec de l'envoi du payload
    }

    // Copier les informations du statut dans la structure fournie en argument
    memcpy(m, &(responseOfP.stat), sizeof(MFS_Stat_t));

    return 0;  // Retourner 0 en cas de succès
}


int MFS_Write(int inum, char *buffer, int block) {
    // Vérifier la validité du nom
    if (IsNameLengthValid(buffer) < 0) {
        return -1;
    }

    // Créer le payload et la réponse
    Payload sentPayload;
    Payload responseOfP;

    // Remplir les champs du payload
    sentPayload.inodeNum = inum;
    memcpy(sentPayload.buffer, buffer, Buffersize);
    sentPayload.block = block;
    sentPayload.Opayload = 2;

    // Appeler la fonction sendPayload
    int rc = sendPayload(&sentPayload, &responseOfP, 3);

    // Vérifier le résultat de l'envoi
    if (rc < 0) {
        return -1;  // En cas d'échec de l'envoi du payload
    }

    return responseOfP.inodeNum;  // Retourner le numéro d'inode en cas de succès
}


int MFS_Read(int inum, char *buffer, int block) {
    // Vérifier la validité du numéro d'inode
    if (inum < 0) {
        return -1;
    }

    // Créer le payload et la réponse
    Payload sentPayload;
    Payload responseOfP;

    // Remplir les champs du payload
    sentPayload.inodeNum = inum;
    sentPayload.block = block;
    sentPayload.Opayload = 3;

    // Appeler la fonction sendPayload
    int rc = sendPayload(&sentPayload, &responseOfP, 3);

    // Vérifier le résultat de l'envoi
    if (rc < 0) {
        return -1;  // En cas d'échec de l'envoi du payload
    }

    // Vérifier si le numéro d'inode est valide
    if (responseOfP.inodeNum < 0) {
        return responseOfP.inodeNum;  // Retourner le code d'erreur
    }

    // Copier les données dans le tampon de sortie
    memcpy(buffer, responseOfP.buffer, Buffersize);

    return responseOfP.inodeNum;  // Retourner le numéro d'inode en cas de succès
}


int MFS_Creat(int pinum, int type, char *name) {
    // Vérifier la validité du nom
    if (IsNameLengthValid(name) < 0) {
        return -1;
    }

    // Créer le payload et la réponse
    Payload sentPayload;
    Payload responseOfP;

    // Remplir les champs du payload
    sentPayload.inodeNum = pinum;
    sentPayload.type = type;
    sentPayload.Opayload = 4;
    strcpy(sentPayload.name, name);

    // Appeler la fonction sendPayload
    int rc = sendPayload(&sentPayload, &responseOfP, 3);

    // Vérifier le résultat de l'envoi
    if (rc < 0) {
        return -1;  // En cas d'échec de l'envoi du payload
    }

    // Retourner le numéro d'inode en cas de succès
    return responseOfP.inodeNum;
}


int MFS_Unlink(int pinum, char *name) {
    // Vérifier la validité du nom
    if (IsNameLengthValid(name) < 0) {
        return -1;
    }

    // Créer le payload et la réponse
    Payload sentPayload;
    Payload responseOfP;

    // Remplir les champs du payload
    sentPayload.inodeNum = pinum;
    sentPayload.Opayload = 5;
    strcpy(sentPayload.name, name);

    // Appeler la fonction sendPayload
    int rc = sendPayload(&sentPayload, &responseOfP, 3);

    // Vérifier le résultat de l'envoi
    if (rc < 0) {
        return -1;  // En cas d'échec de l'envoi du payload
    }

    // Retourner le numéro d'inode en cas de succès
    return responseOfP.inodeNum;
}


int MFS_Shutdown() {
    // Créer le payload et la réponse
    Payload sentPayload, responseOfP;

    // Remplir le champ Opayload du payload
    sentPayload.Opayload = 7;

    // Appeler la fonction sendPayload
    int rc = sendPayload(&sentPayload, &responseOfP, 3);

    // Vérifier le résultat de l'envoi
    if (rc < 0) {
        return -1;  // En cas d'échec de l'envoi du payload
    }

    // Retourner 0 en cas de succès
    return 0;
}


int IsNameLengthValid(char* name) {
    const int maxLength = 27;

    if (strlen(name) > maxLength) {
        // La longueur du nom dépasse la limite autorisée
        return -1;
    } else {
        // La longueur du nom est valide
        return 0;
    }
}

