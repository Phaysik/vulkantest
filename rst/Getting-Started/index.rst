Getting Started
===============
First things first, clone this repository to your machine:

- Through HTTPS

.. code-block:: bash

	git clone https://github.com/Phaysik/xxxxx.git

- Through SSH

.. code-block:: bash

	git clone git@github.com:Phaysik/xxxxx.git

Afterwards, run the :file:`makfileDependencies.sh` file to install all related packages required for this program, or you can view the packages required for each :file:`Makefile` command :doc:`here <../Supplemental/Tables/makefileDependencies>`.

Running the Program
-------------------
Look at the :file:`README.md` file to find a detailed description of each :file:`Makefile` command, also found :doc:`here <../Supplemental/Tables/makefileDescriptions>`, as well as a list of superlevel :file:`Makefile` commands for performing specific actions.

For ease of navigation, below is a list of the superlevel :file:`Makefile` commands and what each of them accomplish, briefly:

- For running the code

.. code-block:: bash

    make run

- For running the test suite

.. code-block:: bash

    make coverage

- For checking memory leaks

.. code-block:: bash

    make valgrind

- For creating the documentation

.. code-block:: bash

    make docs

- For checking linting

.. code-block:: bash

    make tidy