#include "image_processing.h"

/**
*   Aplica filtrele date imaginii primite tot ca parametru.
*   Returneaza daca toate operatiile s-au terminat cu succes sau codul de eroare
*   al operatiei care a esuat.
*/
static PNM_STATUS applyFilters(
    PNM_IMAGE* image,
    int usedBytes,
    int argc,
    char** argv
)
{
    ASSERT(
        image == NULL || argv == NULL,
        ,
        "[applyFilters] Received null pointer parameters.\n",
        PNM_BAD_PARAMS
    );

    int retVal, i;
    uint8_t* aux;
    uint8_t* data;
    double filter[9]; 

    data = malloc(usedBytes * sizeof(*data));
    ASSERT(
        data == NULL,
        ,
        "Unable to allocate memory for additional data.\n",
        PNM_NO_MEMORY
    );

    for (i = 3; i != argc; ++i)
    {
        aux         = data;
        data        = image->data;
        image->data = aux;

        retVal = getFilter(filter, argv[i]);
        ASSERT(retVal != FILTER_OK, free(data), " ", retVal);

        retVal = applyFilter(image, data, filter);
        ASSERT(retVal != PNM_OK, free(data), " ", retVal);
    }

    free(data);

    return PNM_OK;
}

/**
*   Trimite datele de la MASTER catre toate celelalte procese.
*/
static PNM_STATUS scatterData(
    PNM_IMAGE* image,
    int rank,
    int numProc,
    int chunkSize,
    int numBytesScatter
)
{
    int i;

    if (rank != MASTER)
    {
        /* Procesele aloca memorie pentru datele pe care mai apoi le primesc */
        image->data = malloc(numBytesScatter * sizeof(*image->data));
        ASSERT(
            image->data == NULL,
            ,
            "Unable to allocate memory for image data.\n",
            PNM_NO_MEMORY
        );

        MPI_Recv(
            image->data,
            numBytesScatter,
            MPI_CHAR,
            MASTER,
            MPI_ANY_TAG,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );
    } else if (numProc > 1)
    {
        --numProc;

        /* Se trimit datele tuturor proceselor mai putin ultimului */
        for (i = 1; i != numProc; ++i)
        {
            MPI_Send(
                image->data + i * chunkSize - image->width,
                numBytesScatter,
                MPI_CHAR,
                i,
                MPI_TAG_UB,
                MPI_COMM_WORLD
            );
        }

        /**
        * Ultimul proces va avea in general mai putine date, care i se trimit
        * separat
        */
        MPI_Send(
            image->data + numProc * chunkSize - image->width,
            image->height * image->width - numProc * chunkSize + image->width,
            MPI_CHAR,
            numProc,
            MPI_TAG_UB,
            MPI_COMM_WORLD
        );
    }

    return PNM_OK;
}

/**
*   MASTER primeste datele de la toate celelalte procese.
*/
static void gatherData(
    PNM_IMAGE* image,
    int rank,
    int numProc,
    int chunkSize,
    int numBytesGather
)
{
    int i;

    if (rank != MASTER)
    {
        /* Procesele trimit datele lor prelucrate catre MASTER */
        MPI_Send(
            image->data + image->width,
            numBytesGather,
            MPI_CHAR,
            MASTER,
            MPI_TAG_UB,
            MPI_COMM_WORLD
        );
    } else if (numProc > 1)
    {
        --numProc;

        /* Se primesc datele tuturor proceselor mai putin de la ultimul */
        for (i = 1; i != numProc; ++i)
        {
            MPI_Recv(
                image->data + i * numBytesGather,
                numBytesGather,
                MPI_CHAR,
                i,
                MPI_TAG_UB,
                MPI_COMM_WORLD,
                MPI_STATUS_IGNORE
            );
        }

        /**
        * Ultimul proces va avea in general mai putine date, pe care MASTER le
        * primeste separat
        */
        MPI_Recv(
            image->data + numProc * chunkSize,
            image->height * image->width - numProc * chunkSize,
            MPI_CHAR,
            numProc,
            MPI_TAG_UB,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );
    }
}

int processImage(
    PNM_IMAGE* image,
    int rank,
    int numProc,
    int argc,
    char** argv
)
{
    ASSERT(
        image == NULL || argv == NULL,
        ,
        "[processImage] Received null pointer parameters.\n",
        PNM_BAD_PARAMS
    );

    int numBytesScatter, numBytesGather, usedBytes, retVal;

    /**
    * Fiecare proces calculeaza portiunea de imagine pe care va aplica
    * filtrele
    */
    int linesChunk      = ceil((double)image->height / numProc);
    int chunkSize       = linesChunk * image->width;
    int initialHeight   = image->height;
    int start           = rank * linesChunk;
    int end             = MIN(image->height, (rank + 1) * linesChunk);

    /* Se calculeaza parametrii necesari procesarii */
    if (rank != numProc - 1)
    {
        numBytesScatter = chunkSize + 2 * image->width;
        numBytesGather  = chunkSize;
    } else
    {
        numBytesScatter = initialHeight * image->width
                          - (numProc - 1) * chunkSize
                          + image->width;
        numBytesGather  = numBytesScatter - image->width;
    }

    if (rank != MASTER)
    {
        --start;
    }
    if (rank != numProc - 1)
    {
        ++end;
    }

    if (rank == MASTER)
    {
        usedBytes = initialHeight * image->width;
    } else
    {
        usedBytes = image->height * image->width;
    }

    /* Se trimit datele catre toate procesele */ 
    retVal = scatterData(image, rank, numProc, chunkSize, numBytesScatter);
    ASSERT(retVal != PNM_OK, , " ", retVal);

    image->height = end - start;

    /* Se face procesarea efectiva */ 
    retVal = applyFilters(image, usedBytes, argc, argv);
    ASSERT(retVal != PNM_OK, , " ", retVal);

    image->height = initialHeight;

    /* Se primesc datele procesate de la toate procesele */ 
    gatherData(image, rank, numProc, chunkSize, numBytesGather);

    return PNM_OK;
}