#include "image_processing.h"


int main(int argc, char** argv)
{
    PNM_IMAGE image;
    int rank, retVal, numProc;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numProc);

    if (rank == MASTER)
    {
        /* Procesul MASTER citeste intreaga imagine */
        retVal = readImage(argv[1], &image, TRUE);
        ASSERT(retVal != PNM_OK, MPI_Finalize(), " ", retVal);
    } else
    {
        /* Celelalte procese citesc doar headerul */
        retVal = readImage(argv[1], &image, FALSE);
        ASSERT(retVal != PNM_OK, MPI_Finalize(), " ", retVal);
    }

    /* Se trimit, proceseaza si recompun datele */
    retVal = processImage(&image, rank, numProc, argc, argv);
    ASSERT(retVal != PNM_OK, MPI_Finalize(), " ", retVal);

    /* La final, procesul MASTER scrie imaginea in fiser */
    if (rank == MASTER)
    {
        retVal = writeImage(
            &image,
            "# Free Chilipirea!",
            argv[2]
        );
        ASSERT(
            retVal != PNM_OK,
            free(image.data);
            MPI_Finalize(),
            " ",
            retVal
        );
    }

    MPI_Finalize();
    free(image.data);

    return 0;
}