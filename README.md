# Areal: Advanced REactor Archetype Library

[![Build/Test for PR and collaborator push](https://github.com/abachma2/areal/actions/workflows/build_test.yml/badge.svg)](https://github.com/abachma2/areal/actions/workflows/build_test.yml)

Archetype library for use with [Cyclus](fuelcycle.org) to 
model reactors beyond the [Cycamore Reactor archetype](https://fuelcycle.org/user/cycamoreagents.html#cycamore-reactor).

## Dependencies:

| Package           |  Minimum Version   |
|-------------------|--------------------|
| Cyclus            |   1.6              |


## Installation Instructions 

1. Clone this repository
2. `python install.py`

## Running Tests

To run the unit tests: 


    ```
    $ areal_unit_tests
    ```

## Using archetypes
Within the archetype library section of the Cyclus input, you use

```
<spec> <lib>areal</lib> <name>ArchetypeName</name> </spec>
```
in which `ArcheytpeName` is the name of the archetype from 
this library that you wish to use. The file `example_archetype_inputs.xml` 
includes the required and optional (commented out) input parameters to 
define a prototype of each archetype in this library. 


## Contributing
1. Fork this repository
2. Create a working branch on your fork 
3. Make your changes locally
4. Submit a pull request to the source code 
5. Wait for approval and merging on the pull request. 

For more information, please see the [Contributing Guidelines](./CONTRIBUTING.rst).