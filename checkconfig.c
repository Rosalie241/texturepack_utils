#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    FILE* file = fopen(argv[1], "r");

    if (file == NULL)
    {
        perror("fopen");
        return 1;
    }

    int config = -1;
    fread(&config, 4, 1, file);

    fclose(file);

    printf("config = %i\n", config);

    return 0;
}
